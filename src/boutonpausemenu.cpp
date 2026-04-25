#include "GUI/CCControlExtension/CCControlUtils.h"
#include "Geode/ui/Notification.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding_arm/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <matjson.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <Geode/loader/Event.hpp>
#include <fstream>
#include <sstream>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/UILayer.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/binding/EndLevelLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace geode::prelude;

// Forward Declaration
class DiscordAuth;

// Config
// std::string buttonsprite = "deafen.png";
// std::string CLIENT_ID = "1496863624428523690";
// std::string CLIENT_SECRET = "ZWoreN9mvQxKvVw-cJ35puiTOZH1pJhq"; 
std::string CLIENT_ID = "azdeferfrzrrzfrfzzjn";
std::string CLIENT_SECRET = "ZZZZZZZZZZZZZ"; 

// --- LOGIQUE DISCORD RPC ---
class DiscordRPC {
public:
    static inline int s_socket = -1; // On garde le socket ici
    static inline bool s_authenticated = false;

    static void sendFrame(int fd, int opcode, const std::string& payload) {
    if (fd < 0) return;
    uint32_t header[2];
    header[0] = opcode;
    header[1] = static_cast<uint32_t>(payload.length());
    write(fd, header, sizeof(header));
    write(fd, payload.c_str(), payload.length());
    // ✅ PLUS DE read() ICI — c'est ça qui volait les réponses
}

    static void sendDeafenRequest(bool deafen);
    static void connectIfNeeded(); // Nouvelle fonction
};

// --- LOGIQUE AUTH ---
class DiscordAuth {
public:
    static void startAuthorization(CCObject* sender) {
    std::string CLIENT_ID = Mod::get()->getSavedValue<std::string>("discord-client-id", "");
    std::string CLIENT_SECRET = Mod::get()->getSavedValue<std::string>("discord-client-secret", "");
    if (CLIENT_ID.empty() || CLIENT_SECRET.empty()) {
        log::error("Client ID ou Client Secret non configuré !");
    return;
    }
    auto ready = std::make_shared<std::atomic<bool>>(false);
    auto failed = std::make_shared<std::atomic<bool>>(false);

    std::thread([ready, failed]() {
        serverThread(ready, failed);
    }).detach();

    int waited = 0;
    while (!ready->load() && !failed->load() && waited < 2000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited += 50;
    }

    if (failed->load() || !ready->load()) {
        log::error("Le serveur HTTP local n'a pas pu démarrer !");
        geode::Loader::get()->queueInMainThread([]() {
            Notification::create("Erreur: port 8000 indisponible", NotificationIcon::Error)->show();
        });
        return;
    }

    // ✅ rpc.voice.write inclus - fonctionne en mode dev sur ton propre compte
    std::string url = "https://discord.com/oauth2/authorize?client_id=" + CLIENT_ID +
                      "&redirect_uri=http%3A%2F%2Flocalhost%3A8000" +
                      "&response_type=code" +
                      "&scope=rpc%20rpc.voice.write%20identify";
    
    geode::utils::web::openLinkInBrowser(url);
}

    static void serverThread(std::shared_ptr<std::atomic<bool>> ready,
                             std::shared_ptr<std::atomic<bool>> failed) {
        int lsock = socket(AF_INET, SOCK_STREAM, 0);
        if (lsock < 0) { failed->store(true); return; }

        int opt = 1;
        setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(lsock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(8000);
        addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(lsock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            log::error("Erreur Bind: {}", strerror(errno));
            close(lsock);
            failed->store(true);
            return;
        }

        listen(lsock, 1);
        ready->store(true); // ✅ Port ouvert, le navigateur peut se connecter
        log::info("Serveur d'auth actif sur le port 8000...");

        struct timeval timeout;
        timeout.tv_sec = 120;
        timeout.tv_usec = 0;
        setsockopt(lsock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int csock = accept(lsock, nullptr, nullptr);
        if (csock >= 0) {
            char buffer[4096] = {0};
            read(csock, buffer, 4095);
            std::string request(buffer);

            if (request.find("GET /?code=") != std::string::npos) {
                size_t start = request.find("GET /?code=") + 11;
                size_t end = request.find(' ', start);
                if (end == std::string::npos) end = request.size();
                std::string authCode = request.substr(start, end - start);

                log::info("Code reçu: {}", authCode);
                DiscordAuth::exchangeCodeForToken(authCode);

                std::string body = "<html><body style='text-align:center;font-family:sans-serif;"
                                   "background:#1e1e2e;color:white;padding-top:80px;'>"
                                   "<h1 style='color:#5865F2;'>Autorisation reussie !</h1>"
                                   "<p>Vous pouvez retourner sur Geometry Dash.</p></body></html>";
                std::string header = "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/html; charset=utf-8\r\n"
                                     "Content-Length: " + std::to_string(body.length()) + "\r\n"
                                     "Connection: close\r\n\r\n";
                send(csock, (header + body).c_str(), (header + body).length(), 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            } else {
                std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                send(csock, resp.c_str(), resp.length(), 0);
            }
            close(csock);
        } else {
            log::error("accept() timeout: {}", strerror(errno));
        }

        close(lsock);
        log::info("Serveur d'auth fermé.");
    }

    static void exchangeCodeForToken(std::string code) {
        std::string CLIENT_ID = Mod::get()->getSavedValue<std::string>("discord-client-id", "");
        std::string CLIENT_SECRET = Mod::get()->getSavedValue<std::string>("discord-client-secret", "");
        std::string command = "curl -s -X POST https://discord.com/api/oauth2/token "
                          "-H 'Content-Type: application/x-www-form-urlencoded' "
                          "-d 'client_id=" + CLIENT_ID + "' "
                          "-d 'client_secret=" + CLIENT_SECRET + "' "
                          "-d 'grant_type=authorization_code' "
                          "-d 'code=" + code + "' "
                          "-d 'redirect_uri=http://localhost:8000' > /tmp/discord_token.json";

        std::thread([command]() {
            std::system(command.c_str());
            std::ifstream file("/tmp/discord_token.json");
            if (file.is_open()) {
                std::stringstream buf;
                buf << file.rdbuf();
                auto jsonRes = matjson::parse(buf.str());
                if (jsonRes) {
                    auto json = jsonRes.unwrap();
                    if (json.contains("access_token")) {
                        auto tokenRes = json["access_token"].asString();
                        if (tokenRes) {
                            std::string token = tokenRes.unwrap();
                            geode::Loader::get()->queueInMainThread([token]() {
    // Reset du socket pour forcer une reconnexion fraîche avec le nouveau token
                            if (DiscordRPC::s_socket != -1) {
                                close(DiscordRPC::s_socket);
                                DiscordRPC::s_socket = -1;
                                }
                            DiscordRPC::s_authenticated = false;

                            Mod::get()->setSavedValue<std::string>("discord-token", token);
                        log::info("OAuth: Token sauvegardé !");
                            Notification::create("Discord connecté !", NotificationIcon::Success)->show();

    // ✅ Lance la connexion RPC dans un thread séparé
                            std::thread([]() {
                            DiscordRPC::connectIfNeeded();
                            }).detach();
                        });
                        }
                    }
                }
                file.close();
            }
        }).detach();
    }
};

void DiscordRPC::connectIfNeeded() {
    if (s_socket != -1) return;
    std::string CLIENT_ID = Mod::get()->getSavedValue<std::string>("discord-client-id", "");
    std::string CLIENT_SECRET = Mod::get()->getSavedValue<std::string>("discord-client-secret", "");
    if (CLIENT_ID.empty() || CLIENT_SECRET.empty()) {
        log::error("Client ID ou Client Secret non configuré !");
    return;
    }
    std::string token = Mod::get()->getSavedValue<std::string>("discord-token");
    if (token.empty()) {
        log::error("Pas de token OAuth sauvegardé !");
        s_authenticated = false; // ✅
        return;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { s_authenticated = false; return; } // ✅

    std::vector<std::string> searchPaths;
    const char* envTmp = getenv("TMPDIR");
    if (envTmp) searchPaths.push_back(envTmp);
    searchPaths.push_back("/tmp/");

    bool connected = false;
    struct sockaddr_un addr;

    for (const std::string& base : searchPaths) {
        for (int i = 0; i < 10; i++) {
            std::string path = base;
            if (!path.empty() && path.back() != '/') path += "/";
            path += "discord-ipc-" + std::to_string(i);

            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

            if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                log::info("Socket trouvé : {}", path);
                connected = true;
                break;
            }
        }
        if (connected) break;
    }

    if (!connected) {
        close(sock);
        s_authenticated = false; // ✅
        return;
    }

    s_socket = sock;

    auto readResponse = [](int fd) -> std::string {
        uint32_t header[2] = {0};
        int n = read(fd, header, sizeof(header));
        if (n <= 0) return "";
        uint32_t length = header[1];
        if (length == 0 || length > 65536) return "";
        std::string payload(length, '\0');
        read(fd, &payload[0], length);
        return payload;
    };

    // 1. HANDSHAKE
    sendFrame(s_socket, 0, "{\"v\":1,\"client_id\":\"" + CLIENT_ID + "\"}");
    std::string handshakeResp = readResponse(s_socket);
    log::info("Handshake réponse: {}", handshakeResp);

    // 2. AUTHENTICATE
    std::string authPayload =
        "{\"cmd\":\"AUTHENTICATE\","
        "\"args\":{\"access_token\":\"" + token + "\"},"
        "\"nonce\":\"authenticate-1\"}";

    sendFrame(s_socket, 1, authPayload);
    std::string authResp = readResponse(s_socket);
    log::info("Authenticate réponse: {}", authResp);

    if (authResp.find("\"ERROR\"") != std::string::npos) {
        log::error("Authenticate échoué ! Token invalide ou expiré.");
        close(s_socket);
        s_socket = -1;
        s_authenticated = false; // ✅
        Mod::get()->setSavedValue<std::string>("discord-token", "");
        geode::Loader::get()->queueInMainThread([]() {
            Notification::create("Token Discord expiré, re-autorisez l'app !", NotificationIcon::Error)->show();
        });
        return;
    }

    s_authenticated = true; // ✅ Tout s'est bien passé
    log::info("Connecté et authentifié avec succès !");
}

void DiscordRPC::sendDeafenRequest(bool deafen) {
    std::thread([deafen]() {
        connectIfNeeded();

        // ✅ On vérifie le booléen au lieu de juste s_socket >= 0
        if (!s_authenticated || s_socket < 0) {
            log::warn("sendDeafenRequest ignoré : pas authentifié");
            return;
        }

        std::string state = deafen ? "true" : "false";
        std::string payload =
            "{\"cmd\":\"SET_VOICE_SETTINGS\","
            "\"args\":{\"deaf\":" + state + "},"
            "\"nonce\":\"" + std::to_string(time(nullptr)) + "\"}";

        uint32_t hdr[2] = {0};
        sendFrame(s_socket, 1, payload);

        uint32_t header[2] = {0};
        read(s_socket, header, sizeof(header));
        uint32_t length = header[1];
        if (length > 0 && length < 65536) {
            std::string resp(length, '\0');
            read(s_socket, &resp[0], length);
            log::info("SET_VOICE_SETTINGS réponse: {}", resp);

            if (resp.find("\"ERROR\"") != std::string::npos) {
                log::error("SET_VOICE_SETTINGS échoué, reset connexion...");
                close(s_socket);
                s_socket = -1;
                s_authenticated = false; // ✅
            }
        }
    }).detach();
}

// --- POPUP DE RÉGLAGES ---
class MyEditorPopup : public FLAlertLayer {
protected:
    geode::NineSlice* m_inputBG = nullptr;  // Changer ici
    geode::NineSlice* m_inputBG1 = nullptr; // Et ici
    CCTextInputNode* m_percentInput = nullptr;
    CCLabelBMFont* m_titleLabel = nullptr;
    CCMenuItemSpriteExtra* m_deafenBtn = nullptr;
    CCMenuItemSpriteExtra* m_undeafenBtn = nullptr;
    CCMenuItemSpriteExtra* m_settingsBtn = nullptr;
    CCMenuItemSpriteExtra* m_applyBtn = nullptr;
    CCLabelBMFont* m_triggerLabel = nullptr;
    CCMenuItemToggler* m_checkbox1 = nullptr;
    CCLabelBMFont* m_labelcheckbox1 = nullptr;
    CCLabelBMFont* m_triggerLabel1 = nullptr;
    CCTextInputNode* m_percentInput1 = nullptr;
    CCMenuItemToggler* m_checkbox2 = nullptr;
    CCLabelBMFont* m_labelcheckbox2 = nullptr;
    CCMenuItemSpriteExtra* m_pasteid = nullptr;
    CCMenuItemSpriteExtra* m_pastesecret = nullptr;
    CCTextInputNode* m_clientIDInput = nullptr;
    CCTextInputNode* m_clientSecretInput = nullptr;
    geode::NineSlice* idBG = nullptr;
    geode::NineSlice* secretBG = nullptr;
    CCMenuItemSpriteExtra* reconnectBtn = nullptr;

    bool init() override {
        if (!FLAlertLayer::init(150)) return false;

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        
        auto bg = cocos2d::extension::CCScale9Sprite::create("GJ_square01.png");
        bg->setContentSize({ 300.f, 200.f });
        bg->setPosition(winSize / 2);
        m_mainLayer->addChild(bg);

        m_titleLabel = CCLabelBMFont::create("Auto-Deafen", "goldFont.fnt");
        m_titleLabel->setPosition(winSize.width / 2, winSize.height / 2 + 78.f);
        m_titleLabel->setScale(0.7f);
        m_mainLayer->addChild(m_titleLabel);

        m_buttonMenu = CCMenu::create();
        m_mainLayer->addChild(m_buttonMenu);

        //---paf de connexion---

        m_pasteid = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Paste Client ID", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            this,
            menu_selector(MyEditorPopup::onPasteClientID)
        );
        m_pasteid->setPosition({0, 30});
        m_buttonMenu->addChild(m_pasteid);

        m_pastesecret = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Paste Client Secret", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            this,
            menu_selector(MyEditorPopup::onPasteClientSecret)
        );
        m_pastesecret->setPosition({0, -30});
        m_buttonMenu->addChild(m_pastesecret);

        m_deafenBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Connect", "goldFont.fnt", "GJ_button_01.png", 0.6f),
            this,
            menu_selector(MyEditorPopup::onDeafenClick)
        );
        m_deafenBtn->setPosition({0, -70});
        m_buttonMenu->addChild(m_deafenBtn);

        // --- PAGE PRINCIPALE ---

        //---boutons test deafen pour dev---
        // m_deafenBtn = CCMenuItemSpriteExtra::create(
        //     ButtonSprite::create("Deafen", "goldFont.fnt", "GJ_button_01.png", 0.6f),
        //     this, menu_selector(MyEditorPopup::onDeafen)
        // );
        // m_deafenBtn->setPosition({-60, 0});
        // m_buttonMenu->addChild(m_deafenBtn);

        // m_undeafenBtn = CCMenuItemSpriteExtra::create(
        //     ButtonSprite::create("Undeafen", "goldFont.fnt", "GJ_button_06.png", 0.6f),
        //     this, menu_selector(MyEditorPopup::onUndeafen)
        // );
        // m_undeafenBtn->setPosition({60, 0});
        // m_buttonMenu->addChild(m_undeafenBtn);

        // Label
        m_triggerLabel = CCLabelBMFont::create("Deafen at %:", "goldFont.fnt");
        m_triggerLabel->setScale(0.5f);
        m_triggerLabel->setPosition({-40, 48}); // Position relative au centre du menu
        m_triggerLabel->setVisible(false);       // Mis à TRUE
        m_buttonMenu->addChild(m_triggerLabel);

// Fond de l'input
        m_inputBG = geode::NineSlice::create("GJ_square05.png");
        m_inputBG->setColor({ 93, 52, 31 });
        m_inputBG->setOpacity(100);
        m_inputBG->setVisible(false);            
        m_inputBG->setContentSize({ 60, 30 });
        m_inputBG->setPosition({50, 48});
        m_buttonMenu->addChild(m_inputBG);

        m_percentInput = CCTextInputNode::create(50.f, 20.f, "100", "bigFont.fnt");
        m_percentInput->setAllowedChars("0123456789");
        m_percentInput->setPosition(m_inputBG->getPosition() + cocos2d::CCPoint{0, 2});
        m_percentInput->setVisible(false);
        m_buttonMenu->addChild(m_percentInput);

// Bouton Apply
        m_applyBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_01.png", 0.7f),
            this, menu_selector(MyEditorPopup::onApply)
        );
        m_applyBtn->setPosition({0, -70});
        m_applyBtn->setVisible(false);
        m_buttonMenu->addChild(m_applyBtn);

//settings button
        m_settingsBtn = CCMenuItemSpriteExtra::create(
        CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png"),
        this, menu_selector(MyEditorPopup::onToggleSettings)
        );
        m_settingsBtn->setPosition({140, 90});
        m_settingsBtn->setScale(0.8f);
        m_settingsBtn->setVisible(false);
        m_buttonMenu->addChild(m_settingsBtn);

        // Label
        m_triggerLabel1 = CCLabelBMFont::create("Undeafen at %:", "goldFont.fnt");
        m_triggerLabel1->setScale(0.5f);
        m_triggerLabel1->setPosition({-40, 0}); // Position relative au centre du menu
        m_triggerLabel1->setVisible(false);       // Mis à TRUE
        m_buttonMenu->addChild(m_triggerLabel1);

// Fond de l'input
        m_inputBG1 = geode::NineSlice::create("GJ_square05.png");
        m_inputBG1->setColor({ 93, 52, 31 });
        m_inputBG1->setOpacity(100);
        m_inputBG1->setContentSize({ 60, 30 });
        m_inputBG1->setPosition({50, 0});       // Position relative
        m_inputBG1->setVisible(false);            // Mis à TRUE
        m_buttonMenu->addChild(m_inputBG1);

// Champ de texte
        m_percentInput1 = CCTextInputNode::create(50.f, 20.f, "100", "bigFont.fnt");
        m_percentInput1->setAllowedChars("0123456789");
        m_percentInput1->setPosition(m_inputBG1->getPosition() + cocos2d::CCPoint{0, 2});
        m_percentInput1->setVisible(false);
        //m_percentInput1->setVisible(false);
        m_buttonMenu->addChild(m_percentInput1);


        // --- PAGE RÉGLAGES ---

        //reconnect button
        reconnectBtn = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("reconnect", "bigFont.fnt", "GJ_button_01.png", 0.6f),
            this, menu_selector(MyEditorPopup::onreconnectbutton)
        );
        reconnectBtn->setPosition({0, -55});
        reconnectBtn->setVisible(false);
        m_buttonMenu->addChild(reconnectBtn);

//checkbox
        // 1. Préparer les sprites (textures standard de GD)
        auto offSprite = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        auto onSprite = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");

// 2. Créer le Toggler
        m_checkbox1 = CCMenuItemToggler::create(
        offSprite, 
        onSprite, 
        this, 
        menu_selector(MyEditorPopup::onMyCheckboxToggle)
    );

// 3. RÉCUPÉRER LA VALEUR SAUVEGARDÉE (au lieu de true)
        bool savedValue = Mod::get()->getSavedValue<bool>("auto-deafen-enabled", true); 
    m_checkbox1->toggle(savedValue);

// 4. Position et ajout au menu
        m_checkbox1->setPosition({-100, 50});
        m_checkbox1->setScale(0.8f);
        m_buttonMenu->addChild(m_checkbox1);
        m_checkbox1->setVisible(false);

        m_labelcheckbox1 = CCLabelBMFont::create("Activate auto-deafen", "bigFont.fnt");
            m_labelcheckbox1->setScale(0.5f);
            m_labelcheckbox1->setAnchorPoint({0.f, 0.5f}); // Aligné à gauche
            m_labelcheckbox1->setPosition(m_checkbox1->getPosition() + cocos2d::CCPoint{20, 0}); // 20 pixels à droite du bouton
            m_labelcheckbox1->setVisible(false);
            m_buttonMenu->addChild(m_labelcheckbox1);

//checkbox

// 2. Créer le Toggler
        m_checkbox2 = CCMenuItemToggler::create(
        offSprite, 
        onSprite, 
        this, 
        menu_selector(MyEditorPopup::onMyCheckboxToggle1)
    );

// 3. RÉCUPÉRER LA VALEUR SAUVEGARDÉE (au lieu de true)
    savedValue = Mod::get()->getSavedValue<bool>("UndeafenOnPause", true); 
    m_checkbox2->toggle(savedValue);

// 4. Position et ajout au menu
        m_checkbox2->setPosition({-100, 0});
        m_checkbox2->setScale(0.8f);
        m_buttonMenu->addChild(m_checkbox2);
        m_checkbox2->setVisible(false);

        m_labelcheckbox2 = CCLabelBMFont::create("Undeafen on pause", "bigFont.fnt");
            m_labelcheckbox2->setScale(0.5f);
            m_labelcheckbox2->setAnchorPoint({0.f, 0.5f}); // Aligné à gauche
            m_labelcheckbox2->setPosition(m_checkbox2->getPosition() + cocos2d::CCPoint{20, 0}); // 20 pixels à droite du bouton
            m_labelcheckbox2->setVisible(false);
            m_buttonMenu->addChild(m_labelcheckbox2);

        // Fond pour Client ID
        idBG = geode::NineSlice::create("GJ_square05.png");
        idBG->setColor({ 93, 52, 31 });
        idBG->setOpacity(100);
        idBG->setContentSize({ 160, 25 });
        idBG->setPosition({0, 60});
        idBG->setVisible(false);
        m_buttonMenu->addChild(idBG);

// Champ Client ID
        m_clientIDInput = CCTextInputNode::create(150.f, 20.f, "Client ID...", "bigFont.fnt");
        m_clientIDInput->setTouchEnabled(false);
        m_clientIDInput->setPosition({-20, 56});
        m_clientIDInput->setScale(0.7f);
        m_buttonMenu->addChild(m_clientIDInput);

// Fond pour Client Secret
        secretBG = geode::NineSlice::create("GJ_square05.png");
        secretBG->setColor({ 93, 52, 31 });
        secretBG->setOpacity(100);
        secretBG->setContentSize({ 160, 25 });
        secretBG->setPosition({0, 0});
        secretBG->setVisible(false);
        m_buttonMenu->addChild(secretBG);

// Champ Client Secret
        m_clientSecretInput = CCTextInputNode::create(150.f, 20.f, "Client Secret...", "bigFont.fnt");
        m_clientSecretInput->setTouchEnabled(false);
        m_clientSecretInput->setPosition({-20, -5});
        m_clientSecretInput->setScale(0.7f);
        m_buttonMenu->addChild(m_clientSecretInput);

        std::string savedID = Mod::get()->getSavedValue<std::string>("discord-client-id", "");
        if (!savedID.empty()) {
            m_clientIDInput->setString(savedID.c_str());
        }

        std::string savedSecret = Mod::get()->getSavedValue<std::string>("discord-client-secret", "");
        if (!savedSecret.empty()) {
            m_clientSecretInput->setString(savedSecret.c_str());
        }

        auto closeBtn = CCMenuItemSpriteExtra::create(
            CCSprite::createWithSpriteFrameName("GJ_closeBtn_001.png"),
            this, menu_selector(MyEditorPopup::onClose)
        );
        closeBtn->setPosition({-140, 90});
        m_buttonMenu->addChild(closeBtn);

        int saved = Mod::get()->getSavedValue<int>("auto-deafen-trigger-percent", 100);
        m_percentInput->setString(std::to_string(saved).c_str());

        int saved1 = Mod::get()->getSavedValue<int>("auto-undeafen-trigger-percent", 100);
        m_percentInput1->setString(std::to_string(saved1).c_str());

        // ✅ Si déjà authentifié, on saute directement au menu principal
        if (DiscordRPC::s_authenticated) {
            showMainMenu();
        }
        // Sinon : si un token est sauvegardé mais pas encore connecté, on tente la connexion
        else if (!Mod::get()->getSavedValue<std::string>("discord-token").empty()) {
            m_deafenBtn->setVisible(true); // Cache le bouton Connect pendant la tentative
            std::thread([]() {
                DiscordRPC::connectIfNeeded();
            }).detach();
            this->waitForAuth();
        }

        this->setTouchEnabled(true);
        this->setKeypadEnabled(true);
        m_buttonMenu->setTouchPriority(-501);

        return true;
    }

    void onreconnectbutton(CCObject*) {
    log::info("Bouton reconnect cliqué !");
    
    // ✅ Reset complet de la connexion Discord
    if (DiscordRPC::s_socket != -1) {
        close(DiscordRPC::s_socket);
        DiscordRPC::s_socket = -1;
        }
        DiscordRPC::s_authenticated = false;
        Mod::get()->setSavedValue<std::string>("discord-token", ""); // invalide le token aussi
        m_pasteid->setVisible(true);
        m_pastesecret->setVisible(true);
        m_deafenBtn->setVisible(true);
        m_clientIDInput->setVisible(true);
        m_clientSecretInput->setVisible(true);
        // idBG->setVisible(true);
        // secretBG->setVisible(true);
        m_inputBG->setVisible(false);
        m_percentInput->setVisible(false);
        m_inputBG1->setVisible(false);
        m_triggerLabel->setVisible(false);
        m_triggerLabel1->setVisible(false);
        m_settingsBtn->setVisible(false);
        m_checkbox1->setVisible(false);
        m_labelcheckbox1->setVisible(false);
        m_checkbox2->setVisible(false);
        m_labelcheckbox2->setVisible(false);
        reconnectBtn->setVisible(false);
    }

    void onToggleSettings(CCObject*) {
    log::info("Bouton settings cliqué !");

    // Liste des éléments à CACHER
    std::vector<CCNode*> toHide = { 
        m_settingsBtn, m_applyBtn, m_triggerLabel, m_inputBG, 
        m_percentInput, m_inputBG1, m_triggerLabel1, m_percentInput1,secretBG, idBG
    };

    for (auto node : toHide) {
        if (node) node->setVisible(false);
    }

    // Liste des éléments à MONTRER
    if (m_checkbox1) m_checkbox1->setVisible(true);
    if (m_labelcheckbox1) m_labelcheckbox1->setVisible(true);
    if (m_labelcheckbox2) m_labelcheckbox2->setVisible(true);
    if (m_checkbox2) m_checkbox2->setVisible(true);
    if (reconnectBtn) reconnectBtn->setVisible(true);
}

    void onDeafen(CCObject*) { DiscordRPC::sendDeafenRequest(true); }
    void onUndeafen(CCObject*) { DiscordRPC::sendDeafenRequest(false); }
    
    void onApply(CCObject* sender) {
        std::string val = m_percentInput->getString();
        std::string val1 = m_percentInput1->getString();
        if (!val.empty()) {
            Mod::get()->setSavedValue("auto-deafen-trigger-percent", std::stoi(val));
            Mod::get()->setSavedValue("auto-undeafen-trigger-percent", std::stoi(val1));
            Notification::create("Saved!", NotificationIcon::Success)->show();
        }
    }

    void onClose(CCObject*) { this->removeFromParentAndCleanup(true); }

    public:
    static MyEditorPopup* create() {
        auto ret = new MyEditorPopup();
        if (ret && ret->init()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }

    void onDeafenClick(CCObject* sender) {
    // Si déjà authentifié, on affiche directement le menu
    if (DiscordRPC::s_authenticated) {
        this->showMainMenu();
        return;
    }
    // Sinon on lance l'auth et on attend
    DiscordAuth::startAuthorization(nullptr);
    this->waitForAuth();
}

void showMainMenu() {
    m_settingsBtn->setVisible(true);
    m_applyBtn->setVisible(true);
    m_triggerLabel->setVisible(true);
    m_inputBG->setVisible(true);
    m_percentInput->setVisible(true);
    m_inputBG1->setVisible(true);
    m_triggerLabel1->setVisible(true);
    m_percentInput1->setVisible(true);
    m_deafenBtn->setVisible(false);
    m_clientIDInput->setVisible(false);
    m_clientSecretInput->setVisible(false);
    m_pasteid->setVisible(false);
    m_pastesecret->setVisible(false);
    m_clientIDInput->setVisible(false);
    m_clientSecretInput->setVisible(false);
    idBG->setVisible(false);
    secretBG->setVisible(false);
}

int m_authAttempts = 0;

// Et remplace checkAuthStatus et waitForAuth par :
void waitForAuth() {
    m_authAttempts = 0;
    this->schedule(schedule_selector(MyEditorPopup::checkAuthStatus), 0.5f);
}

void checkAuthStatus(float dt) {
    m_authAttempts++;

    if (DiscordRPC::s_authenticated) {
        this->unschedule(schedule_selector(MyEditorPopup::checkAuthStatus));
        m_authAttempts = 0;
        onDeafenClick(nullptr); // ✅ On est déjà sur le thread principal via le scheduler
        return;
    }

    if (m_authAttempts >= 120) {
        this->unschedule(schedule_selector(MyEditorPopup::checkAuthStatus));
        m_authAttempts = 0;
        log::error("Timeout auth Discord");
        Notification::create("Connexion Discord échouée", NotificationIcon::Error)->show();
    }
}

    void onMyCheckboxToggle(cocos2d::CCObject* sender) {
        auto btn = static_cast<CCMenuItemToggler*>(sender);
    // On récupère l'état visuel (si la croix est là ou pas)
    // Attention : Geode inverse parfois la logique selon le sprite, 
    // teste si isToggled() ou !isToggled() correspond à "activé" chez toi.
        bool isEnabled = !btn->isToggled(); 
    
    // ON SAUVEGARDE DANS LE MOD
        Mod::get()->setSavedValue("auto-deafen-enabled", isEnabled);
        log::info("Option auto-deafen changée : {}", isEnabled);

        // 3. CHANGEMENT DE LA VARIABLE DE TEXTURE
    // On vérifie la valeur de isEnabled pour choisir le nom de l'image
    // if (isEnabled) {
    //     buttonsprite = "deafen.png"_spr;
    // } else {
    //     buttonsprite = "undeafen.png"_spr;
    // }
    
    // log::info("Nouvelle texture définie : {}", buttonsprite);
    }

    void onMyCheckboxToggle1(cocos2d::CCObject* sender) {
        auto btn = static_cast<CCMenuItemToggler*>(sender);
    // On récupère l'état visuel (si la croix est là ou pas)
    // Attention : Geode inverse parfois la logique selon le sprite, 
    // teste si isToggled() ou !isToggled() correspond à "activé" chez toi.
        bool isEnabled = !btn->isToggled(); 
    
    // ON SAUVEGARDE DANS LE MOD
        Mod::get()->setSavedValue("UndeafenOnPause", isEnabled);
        log::info("Option deafen on pause : {}", isEnabled);
    }

    void onAuthBridge(CCObject*) {
    DiscordAuth::startAuthorization(nullptr);
    }

    void onPasteClientID(CCObject*) {
        std::string clip = clipboard::read(); // ✅ clipboard::read() pas getClipboardText()
        if (!clip.empty()) {
            m_clientIDInput->setString(clip.c_str()); // ✅ minuscule comme déclaré
            Mod::get()->setSavedValue<std::string>("discord-client-id", clip);
            log::info("Client ID sauvegardé : {}", clip);
        }
    }

    void onPasteClientSecret(CCObject*) {
        std::string clip = clipboard::read(); // ✅ idem
        if (!clip.empty()) {
            m_clientSecretInput->setString(clip.c_str()); // ✅ minuscule comme déclaré
            Mod::get()->setSavedValue<std::string>("discord-client-secret", clip);
            log::info("Client Secret sauvegardé !");
        }
    }
};

// --- HOOKS ---
class $modify(MyPlayLayer, PlayLayer) {
    struct Fields { 
        bool m_hasDeafened = false; 
        bool m_hasUndeafened = false; 
    };

    // Cette fonction est appelée au tout début et à chaque fois que tu crash/recommences
    void resetLevel() {
    PlayLayer::resetLevel();
    m_fields->m_hasDeafened = false;
    m_fields->m_hasUndeafened = false; // Très important !
    DiscordRPC::sendDeafenRequest(false);
    }

    void postUpdate(float dt) {
    PlayLayer::postUpdate(dt);
    
    // On récupère l'état réel sauvegardé
    bool optionActivee = Mod::get()->getSavedValue<bool>("auto-deafen-enabled", true);
    
    // SI L'OPTION EST DÉSACTIVÉE, ON S'ARRÊTE LÀ
    if (!optionActivee) {
        return; 
    }

    int triggerDeafen = Mod::get()->getSavedValue<int>("auto-deafen-trigger-percent", 100);
    int triggerUndeafen = Mod::get()->getSavedValue<int>("auto-undeafen-trigger-percent", 100);
    float current = this->getCurrentPercent();

    // --- LOGIQUE DEAFEN ---
    // On active si : % atteint ET pas encore fait
    if (current >= triggerDeafen && !m_fields->m_hasDeafened) {
        m_fields->m_hasDeafened = true;
        DiscordRPC::sendDeafenRequest(true);
        log::info("Deafen activé à {}%", triggerDeafen);
    }

    // --- LOGIQUE UNDEAFEN ---
    // On active si : % atteint ET on a déjà deafen ET pas encore undeafen
    if (current >= triggerUndeafen && m_fields->m_hasDeafened && !m_fields->m_hasUndeafened) {
        m_fields->m_hasUndeafened = true;
        DiscordRPC::sendDeafenRequest(false);
        log::info("Undeafen activé à {}%", triggerUndeafen);
    }
    }
};

class $modify(MyPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        
        if (Mod::get()->getSavedValue<bool>("UndeafenOnPause", true)) {
            DiscordRPC::sendDeafenRequest(false);
        }

        // Ton code de bouton (logo.png) reste ici...
        auto menu = this->getChildByID("left-button-menu");
    if (!menu) menu = this->getChildByID("center-button-menu");
    if (menu) {
        // ✅ On choisit le sprite selon l'option sauvegardée, avec _spr au compile-time
        CCSprite* sprite = nullptr;
        bool autoDeafenEnabled = Mod::get()->getSavedValue<bool>("auto-deafen-enabled", true);
        if (autoDeafenEnabled) {
            std::string path = Mod::get()->getResourcesDir() / "deafen.png";
            log::info("Chemin sprite: {}", path);
            sprite = CCSprite::create(path.c_str());
        } else {
            std::string path = Mod::get()->getResourcesDir() / "undeafen.png";
            log::info("Chemin sprite: {}", path);
            sprite = CCSprite::create(path.c_str());
        }
        
        // Fallback si la texture n'existe pas
        if (!sprite) sprite = CCSprite::createWithSpriteFrameName("GJ_plusBtn_001.png");
        
        if (sprite) {
            sprite->setScale(1.0f);
            auto button = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(MyPauseLayer::onDeafenButton)
            );
                button->setID("auto-deafen-button"_spr);
                menu->addChild(button);
                menu->updateLayout();
                button->setPositionY(button->getPositionY() - 10);
                button->setPositionX(button->getPositionX() + 10);
            }
        }
    }

    void onDeafenButton(CCObject*) {
        MyEditorPopup::create()->show();
    }
};

// ON UTILISE PLAYLAYER POUR GERER LA REPRISE (Fonctionne avec Espace/Bouton/Echap)
class $modify(MyPlayLayerResume, PlayLayer) {
    void resume() {
        PlayLayer::resume(); // On laisse le jeu reprendre normalement

        // On vérifie si on doit redeafen
        if (Mod::get()->getSavedValue<bool>("UndeafenOnPause", true)) {
            int triggerDeafen = Mod::get()->getSavedValue<int>("auto-deafen-trigger-percent", 100);
            int triggerUndeafen = Mod::get()->getSavedValue<int>("auto-undeafen-trigger-percent", 100);
            float currentPercent = this->getCurrentPercent();

            // Si on est dans la zone où on devrait être sourd
            if (currentPercent >= triggerDeafen && currentPercent < triggerUndeafen) {
                log::info("Redeafen automatique à la reprise du jeu");
                DiscordRPC::sendDeafenRequest(true);
            }
        }
    }
};

class $modify(MyPlayerLayer, PlayerObject) {
    void playerDestroyed(bool p0) {
        PlayerObject::playerDestroyed(p0);

        // On lance l'appel Discord immédiatement
        DiscordRPC::sendDeafenRequest(false);

        // Pour la notification, on peut l'afficher sur le thread principal (Geode s'en occupe)
        // Mais comme l'appel Discord est maintenant asynchrone, le lag aura disparu.
        log::info("Undeafen envoyé !");
    }
};

class $modify(MyEndLevelLayer, EndLevelLayer) {
    void onMenu(cocos2d::CCObject*sender){
        DiscordRPC::sendDeafenRequest(false);
        log::info("Undeafen envoyé depuis EndLevelLayer !");
        EndLevelLayer:: onMenu(sender);
    }
};