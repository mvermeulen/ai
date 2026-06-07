#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "model/Tournament.h"
#include <string>

class WebServer {
public:
    struct Response {
        int statusCode;
        std::string contentType;
        std::string body;
    };

    WebServer(Tournament tournament,
              const std::string& schedulePath,
              double baseRate,
              double alpha,
              double hostAdvantage,
              int defaultIterations = 100000);

    void run(int port);

    Response handleForTests(const std::string& method,
                            const std::string& rawPath,
                            const std::string& body = "");

private:
    Tournament tournament_;
    std::string schedulePath_;
    double baseRate_;
    double alpha_;
    double hostAdvantage_;
    int defaultIterations_;

    std::string handleRequest(const std::string& method,
                              const std::string& rawPath,
                              const std::string& body,
                              int& statusCode,
                              std::string& contentType);

    std::string renderStandingsHtml() const;
    std::string renderSimulationHtml(int iterations) const;
    std::string renderImpactHtml(int iterations) const;

    std::string standingsJson() const;
    std::string simulationJson(int iterations, const std::string& locksStr = "") const;
    std::string impactJson(int iterations) const;

    bool applyResultUpdate(const std::string& body, std::string& error);
    void refreshBracketSlotsFromResults();
    bool persistSchedule() const;

    static std::string jsonEscape(const std::string& value);
    static std::string htmlEscape(const std::string& value);
    static std::string urlDecode(const std::string& value);
    static int parseIterations(const std::string& rawPath, int fallback);
    static std::string parseQueryParam(const std::string& rawPath, const std::string& key);
    static std::string getPathOnly(const std::string& rawPath);
    static std::string buildHttpResponse(int statusCode,
                                         const std::string& contentType,
                                         const std::string& body);
};

#endif // WEB_SERVER_H
