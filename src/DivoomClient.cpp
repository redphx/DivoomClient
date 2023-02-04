// #include "Arduino.h"
#include <ArduinoJson.h>
#include <ArduinoHttpClient.h>
#include <WiFi.h>
#include <AsyncTCP_SSL.hpp>
#include "DivoomClient.h"
#include "libraries/minilzo/minilzo.h"

#define STEP_HEADER_EXPECT 4

const char* DivoomClient::API_DOMAIN = "app.divoom-gz.com";
const char* DivoomClient::FILE_DOMAIN = "f.divoom-gz.com";

const char* DivoomClient::ENDPOINT_USER_LOGIN = "/UserLogin";
const int DivoomClient::REQ_SIZE_USER_LOGIN = 200;
const int DivoomClient::RESP_SIZE_USER_LOGIN = 2000;

const char* DivoomClient::ENDPOINT_CATEGORY_FILE_LIST = "/GetCategoryFileListV2";
const int DivoomClient::REQ_SIZE_CATEGORY_FILE_LIST = 500;
const int DivoomClient::RESP_SIZE_CATEGORY_FILE_LIST = 6000;

const char* DivoomClient::USER_AGENT = "Aurabox/3.1.10 (iPad; iOS 14.8; Scale/2.00)";

const byte DivoomClient::AES_KEY[16] = { 0x37, 0x38, 0x68, 0x72, 0x65, 0x79, 0x32, 0x33, 0x79, 0x32, 0x38, 0x6F, 0x67, 0x73, 0x38, 0x39 };
const byte DivoomClient::AES_IV[16] = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };


int DivoomClient::_parse_current_step;
ParseSuccessHandler DivoomClient::_parse_success_cb;
ParseErrorHandler DivoomClient::_parse_error_cb;

byte DivoomClient::_parse_current_data[DIVOOM_FRAME_SIZE];
int DivoomClient::_parse_current_data_len;
byte DivoomClient::_parse_decrypted_data[DIVOOM_FRAME_SIZE];
int DivoomClient::_parse_decrypted_data_len;
DivoomPixelBeanHeader DivoomClient::_parse_pixel_bean_header;
uint32_t DivoomClient::_parse_last_packet_time;

byte *DivoomClient::_parse_frames_data_ptr;
byte DivoomClient::_parse_current_frame;

CBC<AES128> DivoomClient::_aes;
AsyncSSLClient DivoomClient::_tcp_client = NULL;

DivoomClient::DivoomClient(WiFiClient par_wifi, const char* par_email, const char* par_md5_password) {
  _wifi = par_wifi;

  _email = par_email;
  _md5_password = par_md5_password;
}

DynamicJsonDocument DivoomClient::_Send(const char* method, const char* endpoint, DynamicJsonDocument payload=DynamicJsonDocument(0)) {
  Serial.println(endpoint);
  HttpClient client = HttpClient(_wifi, API_DOMAIN, 80);

  client.beginRequest();
  if (method == "POST") {
    String post_data;
    serializeJson(payload, post_data);

    client.post(endpoint);
    client.sendHeader("User-Agent", USER_AGENT);
    client.sendHeader("Content-Type", "application/json");
    client.sendHeader("Content-Length", post_data.length());
    client.sendHeader("Connection", "close");
    client.beginBody();
    client.print(post_data);
  } else {
    client.get(endpoint);
    client.sendHeader("User-Agent", USER_AGENT);
    client.sendHeader("Connection", "close");
  }
  client.endRequest();

  int doc_size = 1000;
  if (endpoint == ENDPOINT_USER_LOGIN) {
    doc_size = RESP_SIZE_USER_LOGIN;
  } else {
    payload["UserId"] = _user_id;
    payload["Token"] = _token;

    if (endpoint == ENDPOINT_CATEGORY_FILE_LIST) {
      doc_size = RESP_SIZE_CATEGORY_FILE_LIST;
    }
  }

  String response = client.responseBody();
  if (response) {
    Serial.println(response);
    client.stop();

    DynamicJsonDocument doc(doc_size);
    DeserializationError error = deserializeJson(doc, response);
    if (!error) {
      return doc;
    }

    Serial.println(error.c_str());
  }

  return DynamicJsonDocument(0);
}

DynamicJsonDocument DivoomClient::SendGet(const char* endpoint) {
  return _Send("GET", endpoint);
}

DynamicJsonDocument DivoomClient::SendPost(const char* endpoint, DynamicJsonDocument payload) {
  return _Send("POST", endpoint, payload);
}

bool DivoomClient::LogIn() {
  DynamicJsonDocument payload(REQ_SIZE_USER_LOGIN);
  payload["Email"] = _email;
  payload["Password"] = _md5_password;

  DynamicJsonDocument doc = SendPost(ENDPOINT_USER_LOGIN, payload);
  if (doc.isNull()) {
    return false;
  }

  _user_id = doc["UserId"];
  _token = doc["Token"];

  return true;
}

void DivoomClient::GetCategoryFileList(DivoomFileInfoLite *files_list, uint8_t *files_count, uint8_t category_id, uint8_t page, uint8_t per_page) {
  uint16_t start_num = ((page - 1) * per_page) + 1;
  uint16_t end_num = start_num + per_page - 1;

  DynamicJsonDocument payload(REQ_SIZE_CATEGORY_FILE_LIST);
  payload["StartNum"] = start_num;
  payload["EndNum"] = end_num;
  payload["Classify"] = category_id;
  payload["Version"] = 12;
  payload["FileSize"] = 1;  // Animation 16x16
  payload["FileType"] = 1;  // Animation 16x16
  payload["RefreshIndex"] = 0;
  payload["FileSort"] = 0;

  DynamicJsonDocument doc = SendPost(ENDPOINT_CATEGORY_FILE_LIST, payload);
  JsonArray doc_file_list = doc["FileList"];
  *files_count = doc_file_list.size();

  uint8_t i = -1;
  for (JsonObject obj: doc_file_list) {
    ++i;
    long gallery_id = obj["GalleryId"];
    const char* file_id = (const char*) obj["FileId"];

    Serial.println(file_id);
    files_list[i] = {gallery_id, file_id};
  }
}

void DivoomClient::OnParseSuccess(ParseSuccessHandler cb) {
  _parse_success_cb = cb;
}

void DivoomClient::OnParseError(ParseErrorHandler cb) {
  _parse_error_cb = cb;
}

void DivoomClient::ParseFile(const char* fileUrl, byte *frames_data_ptr) {
  _aes.clear();
  _aes.setKey(AES_KEY, 16);
  _aes.setIV(AES_IV, 16);

  _parse_frames_data_ptr = frames_data_ptr;
  _parse_current_data_len = 0;
  _parse_decrypted_data_len = 0;
  _parse_current_frame = 1;

  _parse_current_step = DivoomParseStep::HttpHeader;

  _tcp_client = AsyncSSLClient();
  //_tcp_client.setNoDelay(true);
  _tcp_client.onData(_OnParseData);
  _tcp_client.onError(_OnParseError);
  _tcp_client.onTimeout(_OnParseTimeOut);
  _tcp_client.onPoll(_OnParsePoll);
  _tcp_client.onDisconnect(_OnParseDisconnect);

  // Connect to host
  _tcp_client.connect(FILE_DOMAIN, 80);
  while (!_tcp_client.connected()) {
    delay(100);
  }

  // Generate TCP Download command
  String request = "GET /";
  request += fileUrl;
  request += " HTTP/1.1\r\n";
  request += "Host: ";
  request += FILE_DOMAIN;
  request += "\r\n";
  request += "User-Agent: ";
  request += USER_AGENT;
  request += "\r\n";
  request += "Connection: close\r\n\r\n";

  // Send download command
  _tcp_client.add(request.c_str(), request.length() );
  _tcp_client.send();
}

void DivoomClient::_OnParseData(void* arg, AsyncSSLClient* client, void *data, size_t len) {
  _parse_last_packet_time = millis();

  if (_parse_current_step >= DivoomParseStep::End) {
    return;
  }

  if (_parse_current_step == DivoomParseStep::HttpHeader) {
    _parse_current_step = DivoomParseStep::FileHeader;
    return;
  }

  int data_pos = 0;
  while (true) {
    // Serial.println(data_pos);
    if (_parse_current_step >= DivoomParseStep::End) {
      return;
    }

    if (data_pos >= len) {
      break;
    }

    if (_parse_current_data_len < sizeof(_parse_current_data)) {
      int read_size = sizeof(_parse_current_data) - _parse_current_data_len;
      read_size = min(read_size, static_cast<int>(len - data_pos));
      memcpy(_parse_current_data + _parse_current_data_len, ((byte*) data) + data_pos, read_size);
      _parse_current_data_len += read_size;
      data_pos += read_size;
    }
    if (_parse_current_data_len < DIVOOM_FRAME_SIZE) {
      return;
    }

    if (_parse_current_step == DivoomParseStep::FileHeader) {
      if (_parse_current_data_len >= STEP_HEADER_EXPECT) {
        memcpy(&_parse_pixel_bean_header, _parse_current_data, STEP_HEADER_EXPECT);
        _parse_pixel_bean_header.speed = SWAP_INT16(_parse_pixel_bean_header.speed);

        Serial.println(F("---"));
        Serial.println(_parse_pixel_bean_header.type);
        Serial.println(_parse_pixel_bean_header.total_frames);
        Serial.println(_parse_pixel_bean_header.speed);
        Serial.println(F("---"));

        if (_parse_pixel_bean_header.total_frames > DIVOOM_MAX_FRAMES) {
          _parse_current_step = DivoomParseStep::Skip;
          _tcp_client.close();
          return;
        }

        _parse_current_data_len -= STEP_HEADER_EXPECT;
        memmove(_parse_current_data, _parse_current_data + STEP_HEADER_EXPECT, _parse_current_data_len);
        _parse_current_step = DivoomParseStep::FrameData;
      }
      continue;
    }

    // Decrypt block by block
    int block_size = 16;
    byte encrypted[block_size];
    byte decrypted[block_size];

    div_t div_blocks = div(_parse_current_data_len, block_size);
    int total_blocks = div_blocks.quot;
    for (int i = 0; i < total_blocks; i++) {
      memcpy(encrypted, _parse_current_data + i * block_size, block_size);
      _DecryptAes(decrypted, encrypted, block_size);
      memcpy(_parse_decrypted_data + _parse_decrypted_data_len, decrypted, block_size);
      _parse_decrypted_data_len += block_size;
    }
  
    memmove(_parse_current_data, _parse_current_data + total_blocks * block_size, div_blocks.rem);
    _parse_current_data_len = div_blocks.rem;

    memcpy(_parse_frames_data_ptr + (_parse_current_frame - 1) * DIVOOM_FRAME_SIZE, _parse_decrypted_data, DIVOOM_FRAME_SIZE);

    _parse_decrypted_data_len -= DIVOOM_FRAME_SIZE;
    memmove(_parse_decrypted_data, _parse_decrypted_data + DIVOOM_FRAME_SIZE, _parse_decrypted_data_len);

    ++_parse_current_frame;
    if (_parse_current_frame > _parse_pixel_bean_header.total_frames) {
      _parse_current_step = DivoomParseStep::End;
      return;
    }
  }
}

void DivoomClient::_OnParseError(void* arg, AsyncSSLClient* client, int8_t error) {
  Serial.println(F("[CALLBACK] error"));
  _parse_current_step = DivoomParseStep::Error;
}

void DivoomClient::_OnParseTimeOut(void* arg, AsyncSSLClient* client, uint32_t time) {
  _tcp_client.free();
  Serial.println(F("[CALLBACK] ACK timeout"));
  _parse_current_step = DivoomParseStep::TimeOut;
  _parse_error_cb(-2);
}

void DivoomClient::_OnParsePoll(void* arg, AsyncSSLClient* client) {
  // Serial.println(F("Polling"));
  if (_parse_last_packet_time == 0) {
    _parse_last_packet_time = millis();
    return;
  }

  if (millis() - _parse_last_packet_time > DIVOOM_TIMEOUT) {
    Serial.println(F("Abort"));
    _parse_last_packet_time = 0;
    _parse_current_step = DivoomParseStep::Error;
    client->close();
  }
}

void DivoomClient::_OnParseDisconnect(void* arg, AsyncSSLClient* client) {
  _tcp_client.free();
  Serial.println(F("[CALLBACK] discconnected"));
  if (_parse_current_step == DivoomParseStep::End) {
    if (_parse_success_cb) {
      _parse_success_cb(_parse_pixel_bean_header);
    }
  } else if (_parse_error_cb) {
    _parse_error_cb(-1);
  }
}

void DivoomClient::AbortDownload() {
  _tcp_client.close();
}

void DivoomClient::_DecryptAes(byte decrypted[], byte encrypted[], int len) {
  _aes.decrypt(decrypted, encrypted, len);
}
