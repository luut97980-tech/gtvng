#include "game_backend.hpp"
#include "fake_il2cpp.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <string>
int main(){
    static_assert(vt::FeatureCount==5);
    bool rejected=false;
    try{vt::ios::GameBackend missing([](const char*)->void*{return nullptr;});}
    catch(const std::exception&e){rejected=std::string(e.what()).find("Missing il2cpp_domain_get")!=std::string::npos;}
    assert(rejected);
    using namespace fake;
    auto&ac=cls("ActorCharacter");auto&base=cls("ActorControl");auto&cc=cls("ActorDefaultControlPlayer");cc.parent=&base;
    auto&kmgBase=cls("ActorMiniGameControl");kmgBase.parent=&base;
    auto&kmg=cls("ActorKMGPlayer");kmg.parent=&kmgBase;
    auto&mgUnit=cls("MiniGameUnit","PlayTogether.MiniGame");
    auto&kmgUnit=cls("MiniGameKMGUnit","PlayTogether.MiniGame");kmgUnit.parent=&mgUnit;
    auto&realBoard=cls("MiniGameKMGRealBoard","PlayTogether.MiniGame");realBoard.parent=&kmgUnit;
    auto&kmgSelf=field(kmgUnit,"Self",0x12,8);
    auto&kmgTableId=field(kmgUnit,"<KMGTableID>k__BackingField",0x09,4);
    Object rbObj{&realBoard};
    auto&actorControl=field(ac,"actorControl",0x12,8);auto&uid=field(ac,"<SUID>k__BackingField",0x0a,8);
    auto&actorBase=field(base,"actorBase",0x12,8);auto&terminate=field(base,"_terminate",2,1);
    auto&canJump=field(cc,"_canJump",2,1);
    auto&moveSpeed=field(cc,"_moveSpeed",0x0c,4);
    auto&kmgCanJump=field(kmgBase,"<IsCanJump>k__BackingField",2,1);
    auto&kmgGoal=field(kmgBase,"<IsGoalIn>k__BackingField",2,1);
    auto&transform=field(ac,"baseTransfrom",0x12,8);field(cls("ActorSystem"),"Self",0x12,8);
    Object sys{&cls("ActorSystem")},act{&ac},control{&cc},nextActor{&ac},nextControl{&cc},kmgControl{&kmg};
    auto&trCls=cls("Transform","UnityEngine");
    auto&goCls=cls("GameObject","UnityEngine");
    auto&compCls=cls("Component","UnityEngine");
    trCls.parent=&compCls;
    mgUnit.parent=&compCls;
    Object tr{&trCls},cam{&cls("Camera","UnityEngine")};camera=&cam;
    // ActorControl is a System.Object, NOT a UnityEngine.Object: even when op_Implicit(control) is false,
    // currentControl(actor) and AutoJump must still work via actorBase + _terminate validation.
    control.alive=false;nextControl.alive=false;kmgControl.alive=false;
    fake::system=&sys;actor=&act;set(act,actorControl,&control);set(act,uid,std::int64_t(42));
    set(control,actorBase,&act);set(control,terminate,false);set(control,canJump,false);set(act,transform,&tr);
    set(nextActor,actorControl,&nextControl);set(nextActor,uid,std::int64_t(43));
    set(nextControl,actorBase,&nextActor);set(nextControl,terminate,false);set(nextControl,canJump,false);set(nextControl,moveSpeed,5.0f);set(nextActor,transform,&tr);
    set(kmgControl,actorBase,&nextActor);set(kmgControl,terminate,false);set(kmgControl,kmgCanJump,false);set(kmgControl,kmgGoal,false);
    {
        vt::ios::GameBackend backend(resolve);vt::ios::Model m;
        m.available.fill(true);m.connected=m.controlsAllowed=true;m.jumpIntervalMs=20;
        backend.toggle(m,vt::SpamJump);

        // 0. License invalid check
        m.license.valid=false;
        backend.tick(m,0);
        assert(jumps==0);
        assert(m.status.find("Key bản quyền")!=std::string::npos);
        m.license.valid=true;

        // 1. When jump button is NOT pressed, no jumps must occur (hold-jump behavior)
        m.jumpPressed=false;
        backend.tick(m,0);
        assert(jumps==0);
        assert(m.status.find("Đè nút Nhảy")!=std::string::npos);

        // 2. When jump button is pressed or held down, spam jump triggers
        m.jumpPressed=true;
        backend.tick(m,0);
        assert(jumps==1&&lastJumpTarget==&control&&m.player=="42");
        assert(lastCanJumpDuringOnJump==true&&get<bool>(control,canJump)==false); // _canJump lifted during OnJump and restored after
        backend.tick(m,.01);assert(jumps==1);
        backend.tick(m,.02);assert(jumps==2);

        // 3. When jump button is released, jumping stops immediately
        m.jumpPressed=false;
        backend.tick(m,.04);assert(jumps==2);
        assert(m.status.find("Đè nút Nhảy")!=std::string::npos);

        // 4. Press again after gap
        m.jumpPressed=true;
        backend.tick(m,20);assert(jumps==3); // No catch-up burst after a long gap.
        backend.tick(m,20);assert(jumps==3);
        m.jumpIntervalMs=0;backend.tick(m,20.02);assert(jumps==4);
        backend.tick(m,20.02);assert(jumps==4); // Zero delay still never spins.
        backend.tick(m,20.04);assert(jumps==5);
        backend.toggle(m,vt::SpamJump);backend.tick(m,21);assert(jumps==5);
        backend.toggle(m,vt::Esp);backend.toggle(m,vt::Line);backend.tick(m,22);
        assert(jumps==5&&backend.esp().empty()&&backend.route().empty());
        backend.toggle(m,vt::SpamJump);backend.tick(m,23);assert(jumps==6);
        set(control,terminate,true);backend.tick(m,24);assert(jumps==6);
        set(control,terminate,false);backend.tick(m,25);assert(jumps==7);
        act.alive=false;backend.tick(m,26);assert(jumps==7&&m.player.empty()&&roots.empty());
        actor=&nextActor;backend.tick(m,27);assert(jumps==7&&m.player=="43"); // scene gap holds the new scene for a moment
        backend.tick(m,27.8);assert(jumps==8&&lastJumpTarget==&nextControl);
        // Switch to ActorKMGPlayer and verify PlayTogether.MiniGame.MiniGameKMGUnit.Self resolution + goal stop
        kmgSelf.staticValue=&rbObj;
        set(rbObj,kmgTableId,std::uint32_t(120030));
        set(nextActor,actorControl,&kmgControl);
        backend.tick(m,27.9);assert(jumps==9&&lastJumpTarget==&kmgControl);
        assert(backend.diagnostic().find("MiniGameKMGRealBoard (120030)")!=std::string::npos);
        assert(backend.diagnostic().find("Stealth: off")!=std::string::npos);
        kmgSelf.staticValue=nullptr;
        set(kmgControl,kmgGoal,true);
        backend.tick(m,28.0);assert(jumps==9&&m.status.find("đã về đích")!=std::string::npos);
        set(kmgControl,kmgGoal,false);
        set(nextActor,actorControl,&nextControl);
        jumpException=true;backend.tick(m,28.2);assert(jumps==9&&m.enabled[vt::SpamJump]);
        assert(m.status.find("raised an exception")!=std::string::npos);
        jumpException=false;backend.tick(m,28.3);assert(jumps==9);backend.tick(m,28.5);assert(jumps==10);
        backend.tick(m,.1);assert(jumps==11); // Clock rollback resets the schedule.
        m.jumpIntervalMs=-1;backend.tick(m,.2);assert(jumps==11&&m.status.find("Invalid spam interval")!=std::string::npos);
        backend.stop(m);backend.tick(m,30);assert(jumps==11&&!m.enabled[vt::Esp]&&!m.enabled[vt::Line]&&!m.enabled[vt::SpamJump]);
        backend.stop(m);assert(roots.empty());
        // Test AutoReady (Tự sẵn sàng) with DialogMinigameSimpleInfo & DialogGroupPlay
        auto&simpleCls=cls("DialogMinigameSimpleInfo");simpleCls.parent=&compCls;
        auto&simpleSelf=field(simpleCls,"Self",0x12,8);
        auto&simpleBtn=field(simpleCls,"_readyButton",0x12,8);
        auto&simpleLock=field(simpleCls,"_isLock",2,1);
        auto&simpleReady=field(simpleCls,"_isReady",2,1);
        Object btnGo{&goCls},simpleDlg{&simpleCls};
        simpleSelf.staticValue=&simpleDlg;
        set(simpleDlg,simpleBtn,&btnGo);
        set(simpleDlg,simpleLock,std::uint8_t(0));
        set(simpleDlg,simpleReady,std::uint8_t(0));
        m.jumpIntervalMs=20;
        backend.toggle(m,vt::AutoReady);
        assert(m.enabled[vt::AutoReady]);
        backend.tick(m,29.0);
        assert(readyClicks==1&&m.status.find("Đã bấm Sẵn sàng")!=std::string::npos);
        // Latched on same UI cycle -> does not spam click
        backend.tick(m,29.1);
        assert(readyClicks==1);
        // When dialog reports already ready -> status updates
        set(simpleDlg,simpleReady,std::uint8_t(1));
        backend.tick(m,29.2);
        assert(readyClicks==1&&m.status.find("Game báo đã sẵn sàng")!=std::string::npos);

        // When entering a house (MyHomeObjectManager._loadState == 2 Loading), AutoReady pauses safely without touching UI dialogs
        auto&homeMgrCls=cls("MyHomeObjectManager");
        auto&homeMgrSelf=field(homeMgrCls,"Self",0x12,8);
        auto&homeLoadState=field(homeMgrCls,"_loadState",0x08,4);
        auto&homeCloseFlow=field(homeMgrCls,"_isCloseFlow",2,1);
        Object homeMgr{&homeMgrCls};
        homeMgr.alive=false; // Pure System.Object, not UnityEngine.Object
        homeMgrSelf.staticValue=&homeMgr;
        set(homeMgr,homeLoadState,std::int32_t(2)); // Loading
        set(homeMgr,homeCloseFlow,std::uint8_t(0));
        backend.tick(m,29.25);
        assert(readyClicks==1&&m.status.find("Đang tải nhà")!=std::string::npos);
        set(homeMgr,homeLoadState,std::int32_t(3)); // Complete
        homeMgrSelf.staticValue=nullptr;
        simpleSelf.staticValue=nullptr;

        // Test AutoHammer (Tự động búa) with MiniGameKMGDuel (120019) + ActorDuelPlayer + adjustable delay
        auto&duelCls=cls("MiniGameKMGDuel","PlayTogether.MiniGame");duelCls.parent=&kmgUnit;
        auto&duelPlayerCls=cls("ActorDuelPlayer");duelPlayerCls.parent=&kmgBase;
        auto&duelAttack=field(duelCls,"<IsStateAttack>k__BackingField",2,1);
        auto&duelCanAttack=field(duelCls,"<IsCanAttack>k__BackingField",2,1);
        auto&duelRound=field(duelCls,"<CurrentRound>k__BackingField",0x09,4);
        auto&duelEnd=field(duelCls,"<IsEndGame>k__BackingField",2,1);
        Object duelObj{&duelCls},duelControl{&duelPlayerCls};
        duelControl.alive=false;
        set(duelControl,actorBase,&nextActor);set(duelControl,terminate,false);
        set(nextActor,actorControl,&duelControl);
        set(duelObj,kmgTableId,std::uint32_t(120019));
        set(duelObj,duelRound,std::uint32_t(1));
        set(duelObj,duelAttack,std::uint8_t(1));
        set(duelObj,duelCanAttack,std::uint8_t(0));
        set(duelObj,duelEnd,std::uint8_t(0));
        kmgSelf.staticValue=&duelObj;

        m.hammerDelayMs=100; // 100ms delay after FIGHT
        backend.toggle(m,vt::AutoHammer);
        assert(m.enabled[vt::AutoHammer]);
        backend.tick(m,29.30);
        assert(hammerHits==0&&m.status.find("Chờ chữ FIGHT")!=std::string::npos);

        // FIGHT opens at t=29.40 -> waits 100ms until t=29.50
        set(duelObj,duelCanAttack,std::uint8_t(1));
        backend.tick(m,29.40);
        assert(hammerHits==0&&m.status.find("Chờ delay")!=std::string::npos);
        backend.tick(m,29.45);
        assert(hammerHits==0);
        backend.tick(m,29.50);
        assert(hammerHits==1&&m.status.find("ĐÃ ĐẬP BÚA #1")!=std::string::npos);
        // Latched on same round -> does not spam OnAction
        backend.tick(m,29.55);
        assert(hammerHits==1);
        kmgSelf.staticValue=nullptr;
        set(nextActor,actorControl,&nextControl);
        backend.tick(m,29.60);

        // Test Map 120041 (MiniGameKMGFogMaze) MeshCollider raycasting + Finish_Wall_Collider goal doorway + multi-segment corridor route
        auto&colCls=cls("Collider","UnityEngine");colCls.parent=&compCls;
        auto&meshColCls=cls("MeshCollider","UnityEngine");meshColCls.parent=&colCls;
        auto&boxColCls=cls("BoxCollider","UnityEngine");boxColCls.parent=&colCls;
        auto&fowCls=cls("FowManager");fowCls.parent=&compCls;
        auto&fowStart=field(fowCls,"StartingPoint",0x12,8);
        auto&fowEnd=field(fowCls,"EndingPoint",0x12,8);
        auto&mazeCls=cls("MiniGameKMGFogMaze","PlayTogether.MiniGame");mazeCls.parent=&kmgUnit;
        auto&mazeFow=field(mazeCls,"FogManager",0x12,8);
        auto&mazeEndGo=field(mazeCls,"EndPointGo",0x12,8);

        Object mapRoot{&goCls},ctrlNode{&goCls},hideNode{&goCls},randomMazeRoot{&goCls},variantNode{&goCls};
        Object stageNode{&goCls},meshColGo{&goCls},meshCol{&meshColCls};
        Object envNode{&goCls},defBoardNode{&goCls},boardGo{&goCls},floorBoxCol{&boxColCls};
        Object finishWallRoot{&goCls},fwTop{&goCls},fwBot{&goCls},fwLeft{&goCls},fwRightJamb1{&goCls},fwRightJamb2{&goCls},fwRightLintel{&goCls};
        Object colTop{&boxColCls},colBot{&boxColCls},colLeft{&boxColCls},colRJ1{&boxColCls},colRJ2{&boxColCls},colRLintel{&boxColCls};
        Object startTr{&trCls},endGo{&goCls},fowObj{&fowCls},mazeGame{&mazeCls};

        auto linkChild=[](Object&parent,Object&child,const char*nm){
            child.objName=nm;
            child.parentTr=&parent;
            parent.childTrs.push_back(&child);
        };
        auto attachCol=[](Object&go,Object&col,float cx,float cy,float cz,float ex,float ey,float ez){
            col.ownerGo=&go;
            col.bounds[0]=cx;col.bounds[1]=cy;col.bounds[2]=cz;
            col.bounds[3]=ex;col.bounds[4]=ey;col.bounds[5]=ez;
            go.components.push_back(&col);
        };

        mapRoot.objName="MapRoot";
        linkChild(mapRoot,ctrlNode,"Control");
        linkChild(ctrlNode,hideNode,"HideObject");
        linkChild(hideNode,randomMazeRoot,"RandomMaze_Root");
        linkChild(randomMazeRoot,variantNode,"MazeCreate_S_03_R");
        linkChild(variantNode,stageNode,"maze_stage_03");
        linkChild(stageNode,meshColGo,"maze_stage_03_Collider");
        attachCol(meshColGo,meshCol,0.f,1.f,0.f,8.f,1.f,8.f);

        linkChild(mapRoot,envNode,"Environment");
        linkChild(envNode,defBoardNode,"Default_board");
        linkChild(defBoardNode,boardGo,"Board");
        attachCol(boardGo,floorBoxCol,0.f,-0.5f,0.f,20.f,0.5f,20.f);

        // Goal room around (0,0,0) with X+ doorway (2 jambs + 1 overhead lintel that used to seal the goal)
        linkChild(variantNode,finishWallRoot,"Finish_Wall_Collider");
        linkChild(finishWallRoot,fwTop,"Top");
        linkChild(finishWallRoot,fwBot,"Bot");
        linkChild(finishWallRoot,fwLeft,"Left");
        linkChild(finishWallRoot,fwRightJamb1,"RightJ1");
        linkChild(finishWallRoot,fwRightJamb2,"RightJ2");
        linkChild(finishWallRoot,fwRightLintel,"RightLintel");
        attachCol(fwTop,colTop,0.f,3.f,3.f,3.f,3.f,0.2f);
        attachCol(fwBot,colBot,0.f,3.f,-3.f,3.f,3.f,0.2f);
        attachCol(fwLeft,colLeft,-3.f,3.f,0.f,0.2f,3.f,3.f);
        attachCol(fwRightJamb1,colRJ1,3.f,3.f,-2.1f,0.2f,3.f,0.9f);
        attachCol(fwRightJamb2,colRJ2,3.f,3.f,2.1f,0.2f,3.f,0.9f);
        attachCol(fwRightLintel,colRLintel,3.f,4.18f,0.f,0.2f,2.25f,1.2f); // bottom y = 1.93m > bodyHeight

        tr.pos[0]=-6.0f;tr.pos[1]=0.0f;tr.pos[2]=-6.0f;
        startTr.pos[0]=-6.0f;startTr.pos[1]=0.0f;startTr.pos[2]=-6.0f;
        endGo.pos[0]=0.0f;endGo.pos[1]=0.0f;endGo.pos[2]=0.0f;
        set(fowObj,fowStart,&startTr);
        set(fowObj,fowEnd,&endGo);
        mazeGame.ownerGo=&mapRoot;
        set(mazeGame,kmgTableId,std::uint32_t(120041));
        set(mazeGame,mazeFow,&fowObj);
        set(mazeGame,mazeEndGo,&endGo);

        kmgSelf.staticValue=&mazeGame;
        set(nextActor,actorControl,&kmgControl);
        backend.toggle(m,vt::Esp);
        backend.toggle(m,vt::Line);
        currentBattleState=5;
        backend.tick(m,29.80);
        // Should have 1 ESP marker (ĐÍCH) and a multi-segment corridor route (>= 4 segments) detouring around the MeshCollider wall and entering the X+ portal
        assert(backend.esp().size()==1&&backend.esp()[0].label.find("ĐÍCH")!=std::string::npos);
        assert(backend.route().size()>=3);
        assert(m.status.find("MazeCreate_S_03_R")!=std::string::npos&&m.status.find("Line dẫn đường")!=std::string::npos);

        // When player moves to (4.0, 0.0, -2.0) past the wall, A* recalculates the fastest path from playerPos (not from startPos!)
        tr.pos[0]=4.0f;tr.pos[1]=0.0f;tr.pos[2]=-2.0f;
        backend.tick(m,29.89);
        assert(backend.route().size()==2);
        // Move player back to (-6.0, 0.0, -6.0)
        tr.pos[0]=-6.0f;tr.pos[1]=0.0f;tr.pos[2]=-6.0f;
        // Even when round ends (battleState == 6, hết lượt), the solved corridor line remains visible
        currentBattleState=6;
        backend.tick(m,29.98);
        assert(backend.esp().size()==1&&backend.route().size()>=3);
        assert(m.status.find("hết lượt")!=std::string::npos);
        currentBattleState=5;
        kmgSelf.staticValue=nullptr;
        set(nextActor,actorControl,&nextControl);
        backend.tick(m,29.60);

        backend.stop(m);backend.tick(m,30);assert(jumps==11&&!m.enabled[vt::Esp]&&!m.enabled[vt::Line]&&!m.enabled[vt::SpamJump]&&!m.enabled[vt::AutoReady]&&!m.enabled[vt::AutoHammer]);
        backend.stop(m);assert(roots.empty());
        backend.toggle(m,vt::FeatureCount);assert(!m.enabled[0]&&!m.enabled[1]&&!m.enabled[2]&&!m.enabled[3]&&!m.enabled[4]);
        m.connected=false;backend.tick(m,31);assert(roots.empty());
    }
    assert(roots.empty());
}
