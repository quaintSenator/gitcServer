#pragma once
#include "player.h"
#include <stdio.h>
#include <stdlib.h>
#include "filedb/filedb.h"
#include "../codec/codec.h"
#include "../utils/utils.h"

bool SendPBToClient(uv_tcp_t* client, uint16_t cmd, ::google::protobuf::Message* msg);
#define FILE_READ_LEN_MAX 1024
#define LOGIN_RESULT_OK 0
#define LOGIN_RESULT_FAIL 1
#define LOGIN_RESULT_FAIL_NO_SUCH_PLAYER -2
#define LOGIN_RESULT_FAIL_ALREADY_IN_GAME 3
#define LOGIN_RESULT_FAIL_WRONG 4

#define CREATE_RESULT_SUCCESS 0
#define CREATE_RESULT_FAIL 1
#define CREATE_RESULT_EXISTING_PLAYER 2

#define SERVER_ANNOUNCE_RSP 1006

#define CHARACTER_INITIAL_CAPITALISM 10000
/*
SERVER_ANNOUNCE_RSP
*/
static char s_send_buff[1024*64];
extern ServerCfg g_config;
extern ItemCfg g_item_config;
PlayerMgr g_playerMgr;
PlayerMgr::PlayerMgr() {

}

PlayerMgr::~PlayerMgr() {

}

bool PlayerMgr::init() {
    m_playerMap.clear();
    m_announce = g_config.default_announce;
    return true;
}

bool PlayerMgr::un_init() {
    for (auto it = m_playerMap.begin(); it != m_playerMap.end(); ++it) {
        delete it->second;
    }
    m_playerMap.clear();
    return true;
}

// �ͻ�����ȡ��������
bool PlayerMgr::announce_request(uv_tcp_t* client) {
    fprintf(stdout, "announce request\n");
    SyncAnnounce sync;
    int len = 0;
    sync.set_announce(m_announce);

    len = encode(s_send_buff, SERVER_ANNOUNCE_RSP, sync.SerializeAsString().c_str(), sync.ByteSize());

    return sendData((uv_stream_t*)client, s_send_buff, len);
}

// ����playerID�������
Player* PlayerMgr::find_player(string playerID) {
    for (auto it = m_playerMap.begin(); it != m_playerMap.end(); ++it) {
        if (it->second->PlayerID == playerID)
            return it->second;
    }
    return nullptr;
}

// �ͻ��˶���
bool PlayerMgr::on_client_close(uv_tcp_t* client) {
    cout << "Debuging.. g_playerMgr.on_client_close was called" << endl;
    auto it = m_playerMap.find(client);
    if (it != m_playerMap.end()) {
        _save_player(it->second);
        
        free(it->second);
        m_playerMap.erase(it);
    }
    return true;
}

// �㲥����
bool PlayerMgr::broadcast_announce(string announce) {
    SyncAnnounce sync;
    int len = 0;

    m_announce = announce;
    sync.set_announce(announce);

    if (m_playerMap.empty())
        return true;

    len = encode(s_send_buff, SERVER_ANNOUNCE_RSP, sync.SerializeAsString().c_str(), sync.ByteSize());

    for (auto it = m_playerMap.begin(); it != m_playerMap.end(); ++it) {
        sendData((uv_stream_t*)it->first, s_send_buff, len);
    }
    return true;
}

// ��ҵ�¼// todo ����ҵ6��ʵ����������
bool PlayerMgr::player_login(uv_tcp_t* client, const PlayerLoginReq* req) {
    PlayerLoginRsp rsp;
    PlayerSaveData loadedPlayerSave;//playerSaveֻ���������������ݣ���û����ҵ�����ʱ��Ϣ
    string playerID = req->playerid();
#pragma region LOAD PLAYER
    bool load_player_status = _load_player(playerID, &loadedPlayerSave);// 2. ���ļ��ж�ȡ������ݲ�չ��
    //load�Ĺ�������_load_player����
#pragma endregion
#pragma region CHECK IF PLAYER IN GAME
    if (find_player(playerID) != nullptr) {// 1. �鿴����Ƿ��Ѿ�����Ϸ��
        rsp.set_result(LOGIN_RESULT_FAIL);
        rsp.set_reason("Player Already in Game");
        goto EXIT0;
    }
#pragma endregion
#pragma region  Main Login Logic
    if (!load_player_status) {
        rsp.set_result(LOGIN_RESULT_FAIL);
        rsp.set_reason("NO_SUCH_PLAYER");
    }
    else {
        if (loadedPlayerSave.password() == req->password()) {// 3. ������ҵ�¼���û����������ļ��е��Ƿ�ƥ��
            Player* newLoginPlayer = new Player();//����һ����Ҷ��������ҵ�λ��Ӧ�������������ڳ�ʼ״̬
            generate_new_login_player_by_saveData(newLoginPlayer, loadedPlayerSave);// 4. ������Ҷ��󣬲�����
            m_playerMap.insert(pair<uv_tcp_t*, Player*>(client, newLoginPlayer));// 5. ����Ҷ������m_playerMap�н��й���
            rsp.set_result(LOGIN_RESULT_OK);
            rsp.set_reason("SUCCESS");
            std::cout << "login success, playerID = " + newLoginPlayer->PlayerID + ",Welcome to ProjectSnow" << std::endl;
        }
        else {//�������
            cout << loadedPlayerSave.playerid() + " attempt to login with Wrong Password" << endl;
            rsp.set_result(LOGIN_RESULT_FAIL);
            rsp.set_reason("WRONG PASSWORD");
        }
    }
    // 6. Ӧ��ͻ��˵�¼���
    SendPBToClient(client, SERVER_LOGIN_RSP, &rsp);
#pragma endregion

EXIT0:
    return true;
    // ע�⣺��ҵ��������У����Ƿ����ڲ����󣬷���Ҫ����false����ᵼ�µײ���д�������
}

// ���ע��
bool PlayerMgr::player_create(uv_tcp_t* client, const PlayerCreateReq* req) {
    // todo ����ҵ6��ʵ����������
    /*
PlayerCreateReq
	string PlayerID = 1;
	string Password = 2;
	bytes Name = 3;
    */
    PlayerCreateRsp rsp;
    string playerID = req->playerid();
    char* charPtrToHoldJson = (char*)malloc(FILE_READ_LEN_MAX);
    int jsonLen = load(&playerID[0], charPtrToHoldJson, FILE_READ_LEN_MAX);
    
    if (find_player(playerID)) {// 1. �鿴����Ƿ��Ѿ�����Ϸ�У��������Ϸ�е�¼���򷵻�ʧ��
        rsp.set_result(CREATE_RESULT_EXISTING_PLAYER);
        rsp.set_reason("Already exsisting player");
        goto EXIT0;
    }
    else {
        // 2. ���Դ��ļ��м���������ݣ�������سɹ�����˺��Ѿ���ע�ᣬ����ʧ��
        if (jsonLen != -1) {//����߼������෴�����load�ҵ��˶�Ӧ��player����Ӧ����ֹ
            //����-2 -3 ����ʾ��ȡ�������ˣ�ҲӦ�ý�ֹ����
            rsp.set_result(CREATE_RESULT_EXISTING_PLAYER);
            rsp.set_reason("Already exsisting player");
            goto EXIT0;
        }
        else //jsonLen == -1 û�в�ѯ�������
        {
            //string jsonStr = string(charPtrToHoldJson, 0, jsonLen);
            //Player p = TransformJsonStrToStruct(jsonStr);// 3. ������Ҷ��󲢹���
            Player *p = new Player();
            p->Name = req->name();
            p->Password = req->password();
            p->PlayerID = req->playerid();
            p->Speed = 0;
            p->Position.set_x(0);
            p->Position.set_y(0);
            p->Position.set_z(0);
            p->Rotation.set_x(0);
            p->Rotation.set_y(0);
            p->Rotation.set_z(0);
            p->Rotation.set_w(0);

            p->money = CHARACTER_INITIAL_CAPITALISM;
            p->playerItems = vector<Item>();

            if (!_save_player(p)) {//����playerʧ��  // 4. �������һ�δ��̲���
                rsp.set_result(CREATE_RESULT_FAIL);
                rsp.set_reason("Saving To Disk Fail");
                goto EXIT0;
            }
            else
            {
                rsp.set_result(CREATE_RESULT_SUCCESS);
                rsp.set_reason("CREATE SUCCESS");
                rsp.set_name(p->Name);
                // 5. ����Ҷ������m_playerMap�н��й���
                m_playerMap.insert(pair<uv_tcp_t*, Player*>(client, p));
                goto EXIT0;
            }
        }
        free(charPtrToHoldJson);// 6. Ӧ��ͻ���ע����
    }
EXIT0:
    SendPBToClient(client, SERVER_CREATE_RSP, &rsp);
    return true;
}

// ���ļ��м���������ݣ��ɹ�����true��ʧ�ܷ���false
bool PlayerMgr::_load_player(string playerID, PlayerSaveData* playerData) {
    /*message PlayerSaveData{
    string PlayerID = 1;
    string Password = 2;
    bytes Name = 3;
    }*/
    bool _is_load_ok = false;
    char* charPtrToHoldJson = (char*)malloc(FILE_READ_LEN_MAX);
    int jsonLen = load(&playerID[0], charPtrToHoldJson, FILE_READ_LEN_MAX);
    string jsonStr;
    if (jsonLen <= 0) {//jsonlen = 0��ʾ���ĵ�
        goto EXIT0;
    }
    else {
        jsonStr = string(charPtrToHoldJson, 0, jsonLen);
        Player p = TransformJsonStrToStruct(jsonStr);
        _is_load_ok = true;
        playerData->set_password(p.Password);
        playerData->set_name(p.Name);
        playerData->set_playerid(p.PlayerID);
    }
EXIT0:
    free(charPtrToHoldJson);
    return _is_load_ok;
}

bool PlayerMgr::_load_player_backpack(string playerID, Player* playerData) {//�ӷ������̶������ݣ�д�뵽playerDataλ��
    //����playerData���������������󱳰�respond����playerid��password����Ϣ������д
    bool _is_load_ok = false;
    char* charPtrToHoldJson = (char*)malloc(FILE_READ_LEN_MAX);
    int jsonLen = load(&playerID[0], charPtrToHoldJson, FILE_READ_LEN_MAX);
    string jsonStr;
    if (jsonLen <= 0) {//jsonlen = 0��ʾ���ĵ�
        goto EXIT0;
    }
    else {
        jsonStr = string(charPtrToHoldJson, 0, jsonLen);
        Player p = TransformJsonStrToStruct(jsonStr);
        _is_load_ok = true;
        playerData->playerItems = p.playerItems;//vector = �����
        playerData->money = p.money;
        playerData->Name = p.Name;
        //���������д���ذ���client֮��Ҳ�������õ�
    }
EXIT0:
    free(charPtrToHoldJson);
    return _is_load_ok;
}
// ��������ݱ��浽�ļ��У��ɹ�����true��ʧ�ܷ���false
bool PlayerMgr::_save_player(const Player* player) {
    // todo ����ҵ5��������������л�����̣��ο�save����
    
    string jsonStr = TransformStructToJsonStr(*player);
    if (save(&(player->PlayerID[0]), &jsonStr[0], jsonStr.size())<0) {
        return false;
    }
    return true;
}

bool PlayerMgr::send_backpack_content(uv_tcp_t* client, const GetBackPackContentReq* req) {
    BackPackContentRsp rsp;
    string requestingPlayerId = req->playerid();
    bool status = false;
    for (auto mapPair : g_playerMgr.m_playerMap) {
        Player* curPlayer = mapPair.second;
        if (curPlayer->PlayerID == requestingPlayerId) {
            status = true;
            goto EXIT0;
        }
    }
EXIT0:
    if (status) {
        rsp.set_getresult(PROTO_RESULT_CODE::SERVER_GET_BACKPACK_CONTENT_SUCCESS);
        auto loadedPlayer = new Player();
        _load_player_backpack(requestingPlayerId, loadedPlayer);

        for (auto loaded_player_item : loadedPlayer->playerItems) {
            Item* toAddItem = rsp.add_items();
            toAddItem->set_heapsize(99);//�����õ�
            toAddItem->set_itemid(loaded_player_item.itemid());
            toAddItem->set_price(loaded_player_item.price());//�����õ�
        }
        delete(loadedPlayer);
    }
    else {
        rsp.set_getresult(PROTO_RESULT_CODE::SERVER_GET_SHOP_CONTENT_FAIL);
    }
    SendPBToClient(client, SERVER_CMD_BACKPACK_CONTENT_RSP, &rsp);
    return status;
}

bool PlayerMgr::send_shop_content(uv_tcp_t* client, const GetShopContentReq* req) {
    ShopContentRsp rsp;
    string requestingPlayerId = req->playerid();
    bool status = false;
    for (auto mapPair : g_playerMgr.m_playerMap) {
        Player* curPlayer = mapPair.second;
        if (curPlayer->PlayerID == requestingPlayerId) {
            status = true;
            goto EXIT0;
        }
    }
EXIT0:
    if (status) {
        rsp.set_getresult(PROTO_RESULT_CODE::SERVER_GET_SHOP_CONTENT_SUCCESS);
        int itemConfigSize = g_item_config.ItemDictionaryByItemId.size();
        for (auto mapPair : g_item_config.ItemDictionaryByItemId) {
            Item* toAddItem = rsp.add_items();
            toAddItem->set_itemid(mapPair.first);
            toAddItem->set_price(mapPair.second.price);
            toAddItem->set_heapsize(99);
        }
    }
    else {
        rsp.set_getresult(PROTO_RESULT_CODE::SERVER_GET_SHOP_CONTENT_FAIL);
    }
    SendPBToClient(client, SERVER_CMD_SHOP_CONTENT_RSP, &rsp);
    return status;
}

bool PlayerMgr::buy_item(uv_tcp_t* client, const BuyItemReq* req) {
    BuyItemRsp rsp;
    string requestingPlayerID = req->playerid();
    Player* requestingPlayer = find_player(requestingPlayerID);
    bool status = false;



    /*
BuyItemReq{
	string PlayerID = 1;
	int32 ItemID = 2;
	int32 BuyNumber = 3;
}
BuyItemRsp{
	int32 BuyResult = 1;
	int32 MoneyLeft = 2;
	repeated Item UpdatedItems = 3;
}
    */

}
void update_online_players_syncInfo() {
    ServerSyncInfo broadcastServerSyncInfo;
    cout << "Debuging..update_online_players_syncInfo is called,current player online :" << g_playerMgr.m_playerMap.size() << endl;
    if (g_playerMgr.m_playerMap.size() < 1) {
        return;
    }
    for (auto mapPair : g_playerMgr.m_playerMap) {
# pragma region MyRegion
        //Player* playPtr = mapPair.second;
        //PlayerSyncInfo playerSync;

        //playerSync.set_speed(playPtr->Speed);

        ///*string* getPlayerID = new string();
        //*getPlayerID = playPtr->PlayerID;
        //playerSync.set_allocated_playerid(getPlayerID);*/
        //string* getPlayerID = playerSync.mutable_playerid();
        //*getPlayerID = playPtr->PlayerID;

        ///*string* getPlayerName = new string();
        //*getPlayerName = playPtr->Name;*/
        ////playerSync.set_allocated_playername(getPlayerName);
        //string* getPlayerName = playerSync.mutable_playername();
        //*getPlayerName = playPtr->Name;



        ////auto Position = new Float3();
        ////Position->set_x(playPtr->Position.x());
        ////Position->set_y(playPtr->Position.y());
        ////Position->set_z(playPtr->Position.z());
        ////playerSync.set_allocated_position(Position);

        //auto Position = playerSync.mutable_position();
        //Position->set_x(playPtr->Position.x());
        //Position->set_y(playPtr->Position.y());
        //Position->set_z(playPtr->Position.z());

        ///*   auto Rotation = new Float4();
        //   Rotation->set_x(playPtr->Rotation.x());
        //   Rotation->set_y(playPtr->Rotation.y());
        //   Rotation->set_z(playPtr->Rotation.z());
        //   Rotation->set_w(playPtr->Rotation.w());
        //   playerSync.set_allocated_rotation(Rotation);*/

        //auto Rotation = playerSync.mutable_rotation();
        //Rotation->set_x(playPtr->Rotation.x());
        //Rotation->set_y(playPtr->Rotation.y());
        //Rotation->set_z(playPtr->Rotation.z());
        //Rotation->set_w(playPtr->Rotation.w());


        ////broadcastServerSyncInfo.add_playersyncinfoarray()->CopyFrom(playerSync);
        //broadcastServerSyncInfo.add_players()->CopyFrom(playerSync);
        //playerSync.release_position();
        //playerSync.release_rotation();
        //playerSync.release_playerid();
        //playerSync.release_playername();
#pragma endregion
        PlayerSyncInfo* info = broadcastServerSyncInfo.add_players();
        /*info->set_allocated_position(new Float3(it->second->Position));
        info->set_allocated_rotation(new Float4(it->second->Rotation));*/

        info->mutable_position()->set_x(mapPair.second->Position.x());
        info->mutable_position()->set_y(mapPair.second->Position.y());
        info->mutable_position()->set_z(mapPair.second->Position.z());
                                               
        info->mutable_rotation()->set_x(mapPair.second->Rotation.x());
        info->mutable_rotation()->set_y(mapPair.second->Rotation.y());
        info->mutable_rotation()->set_z(mapPair.second->Rotation.z());
        info->mutable_rotation()->set_w(mapPair.second->Rotation.w());

        info->set_speed(mapPair.second->Speed);
        info->set_playerid(mapPair.second->PlayerID);
        info->set_playername(mapPair.second->Name);
    }
    
    for (auto mapPair : g_playerMgr.m_playerMap) {
        SendPBToClient(mapPair.first, SERVER_SYNC_CMD::SERVER_SYNC_DATA_SEND, &broadcastServerSyncInfo);
        //���ݸ�ÿһ�����
    }
}

void generate_new_login_player_by_saveData(Player* toWrite, PlayerSaveData &data) {
    toWrite->Name = data.name();
    toWrite->Password = data.password();
    toWrite->PlayerID = data.playerid();
    toWrite->Position.set_x(0);
    toWrite->Position.set_y(0);
    toWrite->Position.set_z(0);
    toWrite->Rotation.set_x(0);
    toWrite->Rotation.set_y(0);
    toWrite->Rotation.set_z(0);
    toWrite->Rotation.set_w(0);
    toWrite->Speed = 0;
}