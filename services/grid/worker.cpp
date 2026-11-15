// Shared original-engine worker. Linux builds host game rooms natively;
// Windows builds also generate thumbnails and can still run under Wine.
// No editor, HTTP listener, arbitrary command execution or synthetic renderer.
#ifndef OPENRBX_NATIVE_SERVER
#include "ThumbnailGenerator.h"
#endif
#include "network/API.h"
#include "network/Player.h"
#include "network/Players.h"
#include "network/Server.h"
#include "script/ScriptContext.h"
#include "util/ContentProvider.h"
#include "util/AssetProxy.h"
#include "util/Profiling.h"
#include "util/RunStateOwner.h"
#include "util/Sound.h"
#include "util/StandardOut.h"
#include "v8datamodel/DataModel.h"
#include "v8datamodel/factoryregistration.h"
#include "v8datamodel/CharacterAppearance.h"
#include "v8datamodel/ModelInstance.h"
#include "v8datamodel/PartInstance.h"
#include "v8datamodel/Decal.h"
#include <RakPeerInterface.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <set>
#include <map>

struct AccountAccess
{
    std::string nickname;
    int colors[6];
    std::string tshirt;
};

static std::map<int, AccountAccess> readAccess(const char* statusPath)
{
    std::ifstream file((std::string(statusPath) + ".access").c_str());
    std::map<int, AccountAccess> result;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream input(line);
        int id;
        AccountAccess access;
        if (input >> id >> access.nickname >> access.colors[0] >> access.colors[1] >> access.colors[2]
                  >> access.colors[3] >> access.colors[4] >> access.colors[5]) {
            // Optional field keeps old access files compatible with new workers.
            input >> access.tshirt;
            if (access.tshirt == "-") access.tshirt.clear();
            result[id] = access;
        }
    }
    return result;
}

static void removeAccountPlayer(RBX::Network::Server* server, RBX::Network::Player* player)
{
    if (server->getChildren()) {
        std::vector<boost::shared_ptr<RBX::Instance> > children = *server->getChildren();
        for (unsigned i = 0; i < children.size(); ++i) {
            RBX::Network::Replicator* replicator = dynamic_cast<RBX::Network::Replicator*>(children[i].get());
            boost::shared_ptr<RBX::Instance> remote = replicator ? replicator->getPlayer() : boost::shared_ptr<RBX::Instance>();
            RBX::Network::Player* remotePlayer = dynamic_cast<RBX::Network::Player*>(remote.get());
            if (replicator && (replicator->ownsRemoteInstance(player) ||
                    (remotePlayer && remotePlayer->getUserID() == player->getUserID()))) {
                replicator->closeConnection();
            }
        }
    }
    if (player->getCharacter()) player->getCharacter()->setParent(NULL);
    player->setParent(NULL);
}

static void applyAccountColors(RBX::Network::Player* player, const AccountAccess& access)
{
    RBX::ModelInstance* character = player->getCharacter();
    if (!character) return;
    RBX::BodyColors* colors = dynamic_cast<RBX::BodyColors*>(character->findFirstChildByName("_GridBodyColors"));
    boost::shared_ptr<RBX::BodyColors> hold;
    if (!colors) {
        hold = RBX::Creatable<RBX::Instance>::create<RBX::BodyColors>();
        colors = hold.get();
        colors->setName("_GridBodyColors");
    }
    RBX::BodyColors::prop_HeadColor.setValue(colors, RBX::BrickColor(access.colors[0]));
    RBX::BodyColors::prop_TorsoColor.setValue(colors, RBX::BrickColor(access.colors[1]));
    RBX::BodyColors::prop_LeftArmColor.setValue(colors, RBX::BrickColor(access.colors[2]));
    RBX::BodyColors::prop_RightArmColor.setValue(colors, RBX::BrickColor(access.colors[3]));
    RBX::BodyColors::prop_LeftLegColor.setValue(colors, RBX::BrickColor(access.colors[4]));
    RBX::BodyColors::prop_RightLegColor.setValue(colors, RBX::BrickColor(access.colors[5]));
    if (hold) colors->setParent(character);
    RBX::PartInstance* torso = dynamic_cast<RBX::PartInstance*>(character->findFirstChildByName("Torso"));
    if (!torso) return;
    RBX::Decal* graphic = dynamic_cast<RBX::Decal*>(torso->findFirstChildByName("_NotbloxTShirt"));
    if (access.tshirt.empty()) {
        if (graphic) graphic->setParent(NULL);
    }
    else {
        boost::shared_ptr<RBX::Decal> newGraphic;
        if (!graphic) {
            newGraphic = RBX::Creatable<RBX::Instance>::create<RBX::Decal>();
            graphic = newGraphic.get();
            graphic->setName("_NotbloxTShirt");
            graphic->setFace(RBX::NORM_Z_NEG);
        }
        graphic->setTexture(RBX::TextureId(RBX::ContentId(access.tshirt)));
        if (newGraphic) graphic->setParent(torso);
    }
}

static std::string jsonString(const std::string& text)
{
    std::ostringstream out;
    out << '"';
    for (unsigned i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (c == '"' || c == '\\') out << '\\' << char(c);
        else if (c < 32) out << ' ';
        else out << char(c);
    }
    out << '"';
    return out.str();
}

class WorkerLog : public RBX::Listener<RBX::StandardOut, RBX::StandardOutMessage>
{
    virtual void onEvent(const RBX::StandardOut*, RBX::StandardOutMessage event)
    {
        std::fprintf(stderr, "%s\n", event.message.c_str());
        std::fflush(stderr);
    }
};
#ifndef OPENRBX_WORKER_TEST
int main(int argc, char** argv)
{
    if (argc != 5 && argc != 7) {
        std::fprintf(stderr, "grid-worker server|thumbnail PLACE CONTENT PORT|OUTPUT.b64\n");
        return 2;
    }
    try {
        const char* proxy = std::getenv("OPENRBX_ASSET_PROXY");
        RBX::AssetProxy::setEnabled(!proxy || std::string(proxy) != "0");
        bool thumbnail = std::string(argv[1]) == "thumbnail" || std::string(argv[1]) == "thumbnail-offline";
        if (std::string(argv[1]) == "thumbnail-offline") {
            SetEnvironmentVariableA("OPENRBX_THUMBNAIL_OFFLINE", "1");
            std::fprintf(stderr, "Using missing-texture fallback for unavailable remote assets\n");
        }
        RBX::Profiling::init(true);
        RBX::FactoryRegistrator factories;
        RBX::Network::API::init("0, 3, 676, 0");
        RBX::Soundscape::SoundService::soundDisabled = true;
        RBX::ContentProvider::singleton().setAssetFolder(argv[3]);
        WorkerLog log;
        RBX::startListening(RBX::StandardOut::singleton(), &log);
        boost::shared_ptr<RBX::DataModel> model = RBX::DataModel::createDataModel();
        {
            RBX::DataModel::Lock lock(model);
            // Establish backend mode before loading place scripts.
            if (std::string(argv[1]) == "server") model->create<RBX::Network::Server>();
            model->loadContent(RBX::ContentId::fromFile(argv[2]));
        }
        if (thumbnail) {
#ifdef OPENRBX_NATIVE_SERVER
            throw std::runtime_error("Thumbnails use the separate Wine worker");
#else
            RBX::DataModel::Lock lock(model);
            std::string data = model->create<ThumbnailGenerator>()->click("PNG", 480, 300, false);
            std::ofstream output(argv[4], std::ios::binary);
            output.write(data.data(), data.size());
            if (!output) throw std::runtime_error("Cannot write thumbnail");
            std::fprintf(stderr, "THUMBNAIL_READY\n");
#endif
        } else if (std::string(argv[1]) == "server") {
            int port = std::atoi(argv[4]);
            int limit = argc == 7 ? std::atoi(argv[5]) : 8;
            if (limit < 1 || limit > 32) throw std::runtime_error("Invalid player limit");
            if (port < 1024 || port > 65535) throw std::runtime_error("Invalid UDP port");
            {
                RBX::DataModel::Lock lock(model);
                model->create<RBX::Network::Server>()->start(port, 10);
                model->create<RBX::Network::Server>()->setPlayerLimit(limit);
                model->create<RBX::Network::Players>()->setMaxPlayers(limit);
                model->create<RBX::ScriptContext>()->executeInNewThread(
                    RBX::Security::TrustedWebService,
                    "game.Players.PlayerAdded:connect(function(p) "
                    "while p.Parent do local c=p.Character "
                    "local h=c and c:findFirstChild('Humanoid') "
                    "if not c or not c.Parent or (h and h.Health<=0) then wait(3) "
                    "if p.Parent and p.Character==c then p:LoadCharacter() end end "
                    "wait(.1) end end)", "GridRespawn");
                model->create<RBX::RunService>()->run();
            }
            std::fprintf(stderr, "SERVER_READY %d\n", port);
            std::fflush(stderr);
            std::set<int> seenPlayers;
            unsigned heartbeat = 0;
            for (;;) {
                Sleep(33);
                RBX::DataModel::Lock lock(model);
                if (++heartbeat < 30) continue;
                heartbeat = 0;
                boost::shared_ptr<const std::vector<boost::shared_ptr<RBX::Instance> > > players =
                    model->create<RBX::Network::Players>()->getPlayers();
                unsigned characters = 0;
                std::map<int, AccountAccess> access;
                if (argc == 7) access = readAccess(argv[6]);
                std::set<int> currentPlayers;
                std::ostringstream status;
                status << "{\"players\":[";
                for (unsigned i = 0; i < players->size(); ++i) {
                    RBX::Network::Player* player = dynamic_cast<RBX::Network::Player*>((*players)[i].get());
                    if (!player) continue;
                    if (argc == 7) {
                        std::map<int, AccountAccess>::const_iterator entry = access.find(player->getUserID());
                        if (entry == access.end() || entry->second.nickname != player->getName()) {
                            std::fprintf(stderr, "ACCOUNT_ACCESS_REVOKED id=%d\n", player->getUserID());
                            removeAccountPlayer(model->create<RBX::Network::Server>(), player);
                            continue;
                        }
                        applyAccountColors(player, entry->second);
                    }
                    if (player && player->getCharacter()) ++characters;
                    currentPlayers.insert(player->getUserID());
                    seenPlayers.insert(player->getUserID());
                    if (currentPlayers.size() > 1) status << ',';
                    status << "{\"id\":" << player->getUserID() << ",\"nickname\":" << jsonString(player->getName())
                           << ",\"colors\":[";
                    if (player->getCharacter()) {
                        const char* names[] = {"Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"};
                        for (int part = 0; part < 6; ++part) {
                            RBX::PartInstance* body = dynamic_cast<RBX::PartInstance*>(
                                player->getCharacter()->findFirstChildByName(names[part]));
                            if (part) status << ',';
                            status << (body ? body->getColor().asInt() : 0);
                        }
                    }
                    status << "]}";
                }
                status << "],\"departed\":[";
                bool firstDeparted = true;
                for (std::set<int>::const_iterator it = seenPlayers.begin(); it != seenPlayers.end(); ++it) {
                    if (currentPlayers.find(*it) == currentPlayers.end()) {
                        if (!firstDeparted) status << ',';
                        status << *it;
                        firstDeparted = false;
                    }
                }
                status << "],\"clients\":" << model->create<RBX::Network::Server>()->getClientCount() << '}';
                if (argc == 7) {
                    std::string tmp = std::string(argv[6]) + ".tmp";
                    { std::ofstream file(tmp.c_str(), std::ios::binary); file << status.str(); }
                    MoveFileExA(tmp.c_str(), argv[6], MOVEFILE_REPLACE_EXISTING);
                }
                std::fprintf(stderr, "GRID_STATUS clients=%d players=%u characters=%u\n",
                    model->create<RBX::Network::Server>()->getClientCount(), unsigned(players->size()), characters);
                std::fflush(stderr);
            }
        } else throw std::runtime_error("Unknown mode");
        RBX::DataModel::closeDataModel(model, true);
        RBX::stopListening(RBX::StandardOut::singleton(), &log);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "WORKER_ERROR %s\n", error.what());
        return 1;
    }
}
#endif
