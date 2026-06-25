#include "email_service.hpp"
#include <curl/curl.h>
#include <iostream>
#include <cstring>

namespace billminder {

struct UploadStatus {
    size_t lines_read;
    std::vector<std::string> payload;
};

static size_t payload_source(char *ptr, size_t size, size_t nmemb, void *userp) {
    UploadStatus *upload_ctx = static_cast<UploadStatus *>(userp);
    
    if ((size == 0) || (nmemb == 0) || ((size*nmemb) < 1)) {
        return 0;
    }
    
    if (upload_ctx->lines_read < upload_ctx->payload.size()) {
        const std::string& line = upload_ctx->payload[upload_ctx->lines_read];
        size_t len = line.length();
        if (len > size * nmemb) len = size * nmemb; // Safety bound
        std::memcpy(ptr, line.c_str(), len);
        upload_ctx->lines_read++;
        return len;
    }
    
    return 0;
}

EmailService::EmailService(const std::string& smtp_url, const std::string& username, const std::string& password, const std::string& to_address)
    : smtp_url_(smtp_url), username_(username), password_(password), to_address_(to_address) {}

bool EmailService::send_email(const std::string& subject, const std::string& body) {
    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *recipients = NULL;
    
    UploadStatus upload_ctx;
    upload_ctx.lines_read = 0;
    upload_ctx.payload.push_back("To: " + to_address_ + "\r\n");
    upload_ctx.payload.push_back("From: " + username_ + " (BillMinder)\r\n");
    upload_ctx.payload.push_back("Subject: " + subject + "\r\n");
    upload_ctx.payload.push_back("\r\n");
    upload_ctx.payload.push_back(body + "\r\n");

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, username_.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, password_.c_str());
        curl_easy_setopt(curl, CURLOPT_URL, smtp_url_.c_str());

        // For Gmail, we use smtps://smtp.gmail.com:465
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL);

        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, username_.c_str());
        recipients = curl_slist_append(recipients, to_address_.c_str());
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(recipients);
        curl_easy_cleanup(curl);
    }
    return (res == CURLE_OK);
}

} // namespace billminder
