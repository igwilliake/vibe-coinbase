#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NameValueCollection.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/SSLManager.h>
#include <Poco/Net/PrivateKeyPassphraseHandler.h>
#include <Poco/HMACEngine.h>
#include <Poco/SHA2Engine.h>
#include <Poco/Base64Encoder.h>
#include <Poco/StreamCopier.h>
#include <Poco/Buffer.h>
#include <Poco/Util/ServerApplication.h>
#include <Poco/URI.h>

#include <ctime>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace Poco::Net;
using namespace Poco;
using namespace std;

// Replace with your actual credentials
const std::string API_KEY = "your_api_key";
const std::string API_SECRET = "your_api_secret";
const std::string API_PASSPHRASE = "your_api_passphrase";

// HMAC SHA256 signer
std::string createSignature(const std::string& timestamp, const std::string& method, const std::string& requestPath, const std::string& body) {
    std::string prehash = timestamp + method + requestPath + body;

    HMACEngine<SHA2Engine256> hmac(API_SECRET);
    hmac.update(prehash);
    const std::vector<unsigned char>& digest = hmac.digest();
    std::ostringstream oss;
    Base64Encoder encoder(oss);
    encoder.write(reinterpret_cast<const char*>(digest.data()), digest.size());
    encoder.close();

    return oss.str();
}

int main() {
	Poco::Util::ServerApplication app;
    Poco::Net::initializeSSL();

    try {
        URI uri("wss://advanced-trade-ws.coinbase.com");
        HTTPSClientSession session(uri.getHost(), uri.getPort());

        std::string path = uri.getPathAndQuery();
        if (path.empty()) path = "/";

        std::string timestamp = std::to_string(std::time(nullptr));
        std::string method = "GET";
        std::string requestPath = "/"; // no REST endpoint here, but still required
        std::string body = "";         // empty string for GET

        std::string signature = createSignature(timestamp, method, requestPath, body);

        HTTPRequest request(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
        request.set("CB-ACCESS-KEY", API_KEY);
        request.set("CB-ACCESS-SIGN", signature);
        request.set("CB-ACCESS-TIMESTAMP", timestamp);
        request.set("CB-ACCESS-PASSPHRASE", API_PASSPHRASE);
        request.set("User-Agent", "CoinbasePrimeClient/1.0");

        HTTPResponse response;
        WebSocket ws(session, request, response);

        // Send a subscription message for BTC ticker
        std::string subscribeMsg = R"({
            "type": "subscribe",
            "channel": "market_trades",
            "product_ids": ["BTC-USD"]
        })";

        ws.sendFrame(subscribeMsg.c_str(), subscribeMsg.length(), WebSocket::FRAME_TEXT);

        constexpr size_t bufferSize = 32*1024;
        auto buffer = std::make_unique<char[]>(bufferSize);
        int flags;
        int n;

        while (true) {
            n = ws.receiveFrame(buffer.get(), bufferSize, flags);
            std::string msg(buffer.get(), n);
            std::cout << "Received: " << msg << std::endl;
        }

    } catch (Exception& ex) {
        std::cerr << "Exception: " << ex.displayText() << std::endl;
    }

    Poco::Net::uninitializeSSL();
    return 0;
}
