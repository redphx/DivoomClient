#ifndef DIVOOMCLIENT_H
#define DIVOOMCLIENT_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include "libraries/aes/Crypto.h"
#include "libraries/aes/AES.h"
#include "libraries/aes/CBC.h"
#include <AsyncTCP_SSL.hpp>

#ifndef DIVOOM_MAX_SIZE
  #define DIVOOM_MAX_SIZE 16
#endif

#ifndef DIVOOM_MAX_FRAMES
  #define DIVOOM_MAX_FRAMES 60
#endif

#ifndef DIVOOM_TIMEOUT
  #define DIVOOM_TIMEOUT 10000
#endif

#define DIVOOM_FRAME_SIZE DIVOOM_MAX_SIZE * DIVOOM_MAX_SIZE * 3
#define DIVOOM_ALL_FRAMES_SIZE DIVOOM_FRAME_SIZE * DIVOOM_MAX_FRAMES

#define SWAP_INT16(x) (x >> 8) | (x << 8);

struct DivoomFileInfoLite {
  long gallery_id;
  const char* file_id;
  // String file_name;
  // String content;
};


struct DivoomPixelBeanHeader {
  uint8_t type;
  uint8_t total_frames;
  uint16_t speed;
};


struct DivoomParseStep {
  enum step {
    HttpHeader,
    FileHeader,
    FrameSize,
    FrameData,
    End,
    Skip,
    Error,
    TimeOut,
  };
};

typedef std::function<void(int8_t error)> ParseErrorHandler;
typedef std::function<void(DivoomPixelBeanHeader header)> ParseSuccessHandler;


class DivoomClient {
  public:
    DivoomClient(WiFiClient par_wifi, const char* par_email, const char* par_md5_password);

    bool LogIn();
    DynamicJsonDocument SendGet(const char* endpoint);
    DynamicJsonDocument SendPost(const char* endpoint, DynamicJsonDocument payload);
    void GetCategoryFileList(DivoomFileInfoLite *files_list, uint8_t *files_count, uint8_t category_id, uint8_t page, uint8_t per_page);

    void OnParseSuccess(ParseSuccessHandler cb);
    void OnParseError(ParseErrorHandler cb);
    void ParseFile(const char* url, byte frames_data[DIVOOM_ALL_FRAMES_SIZE]);
    void AbortDownload();

  private:
    static const char* API_DOMAIN;
    static const char* FILE_DOMAIN;

    static const char* ENDPOINT_USER_LOGIN;
    static const int REQ_SIZE_USER_LOGIN;
    static const int RESP_SIZE_USER_LOGIN;

    static const char* ENDPOINT_CATEGORY_FILE_LIST;
    static const int REQ_SIZE_CATEGORY_FILE_LIST;
    static const int RESP_SIZE_CATEGORY_FILE_LIST;

    static const char* USER_AGENT;

    static const byte AES_KEY[16];
    static const byte AES_IV[16];

    static const int FRAME_SIZE;

    WiFiClient _wifi;

    const char* _email;
    const char* _md5_password;

    long _user_id;
    long _token;

    DynamicJsonDocument _Send(const char* method, const char* endpoint, DynamicJsonDocument payload);

    static int _parse_current_step;
    static ParseSuccessHandler _parse_success_cb;
    static ParseErrorHandler _parse_error_cb;

    static byte _parse_current_data[DIVOOM_FRAME_SIZE];
    static int _parse_current_data_len;
    static byte _parse_decrypted_data[DIVOOM_FRAME_SIZE];
    static int _parse_decrypted_data_len;
    static DivoomPixelBeanHeader _parse_pixel_bean_header;
    static uint32_t _parse_last_packet_time;

    static byte *_parse_frames_data_ptr;
    static byte _parse_current_frame;

    static AsyncSSLClient _tcp_client;

    static void _OnParseData(void* arg, AsyncSSLClient* client, void *data, size_t len);
    static void _OnParseError(void* arg, AsyncSSLClient* client, int8_t error);
    static void _OnParseTimeOut(void* arg, AsyncSSLClient* client, uint32_t time);
    static void _OnParsePoll(void* arg, AsyncSSLClient* client);
    static void _OnParseDisconnect(void* arg, AsyncSSLClient* client);

    static CBC<AES128> _aes;
    static void _DecryptAes(byte decrypted[], byte encrypted[], int len);
};

#endif  // DIVOOMCLIENT_H
