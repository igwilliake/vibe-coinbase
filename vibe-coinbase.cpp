#include <iostream>
#include <string>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/WebSocket.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/Buffer.h>
#include <Poco/URI.h>
//#include <Poco/SSLInitializer.h>

using namespace Poco::Net;
using namespace Poco;
using namespace Poco::JSON;
using namespace std;

int main() {
    Poco::Net::initializeSSL();

    try {
        URI uri("wss://ws-feed.exchange.coinbase.com");
        HTTPSClientSession session(uri.getHost(), uri.getPort());

        string path(uri.getPathAndQuery());
        if (path.empty()) path = "/";

        HTTPRequest request(HTTPRequest::HTTP_GET, path, HTTPMessage::HTTP_1_1);
        HTTPResponse response;

        WebSocket ws(session, request, response);

        // Subscribe to heartbeat and ticker
        string subMsg = R"({
            "type": "subscribe",
            "channels": [
                {
                    "name": "heartbeat",
                    "product_ids": ["BTC-USD"]
                },
                {
                    "name": "ticker",
                    "product_ids": ["BTC-USD"]
                }
            ]
        })";

        ws.sendFrame(subMsg.c_str(), subMsg.length(), WebSocket::FRAME_TEXT);

        // Buffer for receiving messages
        char buffer[4096];
        int flags;
        int n;

        cout << "Listening for heartbeat and BTC ticker messages..." << endl;

        while (true) {
            n = ws.receiveFrame(buffer, sizeof(buffer), flags);
            string msg(buffer, n);

            try {
                Parser parser;
                Poco::Dynamic::Var result = parser.parse(msg);
                Object::Ptr obj = result.extract<Object::Ptr>();

                string type = obj->getValue<string>("type");

                if (type == "heartbeat") {
                    cout << "[Heartbeat] Sequence: " << obj->getValue<int>("sequence")
                         << ", Last trade ID: " << obj->getValue<int>("last_trade_id") << endl;
                } else if (type == "ticker") {
                    string price = obj->getValue<string>("price");
                    string time = obj->getValue<string>("time");
                    cout << "[Ticker] BTC-USD Price: $" << price << " at " << time << endl;
                } else {
                    cout << "[Other] " << msg << endl;
                }
            } catch (Exception& ex) {
                cerr << "[Parse Error] " << ex.displayText() << " - Raw message: " << msg << endl;
            }
        }
    } catch (Exception& ex) {
        cerr << "Exception: " << ex.displayText() << endl;
    }

    Poco::Net::uninitializeSSL();
    return 0;
}
