#define OPENRBX_WORKER_TEST
#include "../worker.cpp"
#include "v8datamodel/Value.h"
#include "v8datamodel/Workspace.h"
#include "security/SecurityContext.h"
#include "GetTime.h"

int main(int argc,char** argv) {
    try {
        RBX::Security::Impersonator security(RBX::Security::TrustedCOM);
        RBX::Profiling::init(true);
        RBX::FactoryRegistrator factories;
        RBX::Network::API::init("0, 3, 676, 0");
        RBX::Soundscape::SoundService::soundDisabled=true;
        if(argc>1) RBX::ContentProvider::singleton().setAssetFolder(argv[1]);
        WorkerLog log; RBX::startListening(RBX::StandardOut::singleton(),&log);
        boost::shared_ptr<RBX::DataModel> model=RBX::DataModel::createDataModel();
        {
            RBX::DataModel::Lock lock(model);
            model->create<RBX::Network::Server>();
            model->create<RBX::ScriptContext>()->executeInNewThread(RBX::Security::TrustedWebService,
                "local floor=Instance.new('Part',game.Workspace) floor.Name='NativeFloor' "
                "floor.Anchored=true floor.Size=Vector3.new(100,2,100) floor.Position=Vector3.new(0,0,0) "
                "local ball=Instance.new('Part',game.Workspace) ball.Name='NativeFallingPart' "
                "ball.Size=Vector3.new(2,2,2) ball.Position=Vector3.new(0,30,0) "
                "local count=Instance.new('IntValue',game.Workspace) count.Name='NativeStepCount' "
                "game:service('RunService').Stepped:connect(function() count.Value=count.Value+1 end)",
                "native-linux-simulation-test");
            model->create<RBX::RunService>()->run();
        }
        RakNetTimeNS start=RakNet::GetTimeNS();
        // This engine's legacy gravity is much lower than modern Roblox's;
        // allow the part to reach the floor without changing its constants.
        Sleep(4200);
        {
            RBX::DataModel::Lock lock(model);
            RBX::PartInstance* ball=dynamic_cast<RBX::PartInstance*>(model->getWorkspace()->findFirstChildByName("NativeFallingPart"));
            RBX::Value<int,RBX::sIntValue>* count=dynamic_cast<RBX::Value<int,RBX::sIntValue>*>(model->getWorkspace()->findFirstChildByName("NativeStepCount"));
            if(!ball || !count) throw std::runtime_error("Lua did not create simulation fixtures");
            float y=ball->getCoordinateFrame().translation.y;
            double elapsed=(RakNet::GetTimeNS()-start)/1000000.0;
            std::fprintf(stderr,"NATIVE_PHYSICS steps=%d elapsed=%.3f hz=%.2f falling_part_y=%.3f\n",count->getValue(),elapsed,count->getValue()/elapsed,y);
            if(count->getValue()<85 || count->getValue()>145) throw std::runtime_error("Simulation is not running at its original speed");
            if(!std::isfinite(y) || y<0.5f || y>4.0f) throw std::runtime_error("Gravity/collision regression");
        }
        RBX::DataModel::closeDataModel(model,true);
        RBX::stopListening(RBX::StandardOut::singleton(),&log);
        std::puts("NATIVE_LUA_30HZ_GRAVITY_COLLISION_PASS");
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"NATIVE_TEST_FAILED %s\n",error.what()); return 1; }
}
