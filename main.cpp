// DropMag (Android) - magazen jatuh saat reload, dengan suara.
// Target: GTA SA Android, dimuat lewat AML (Android Mod Loader).
//   - arm64-v8a  (64-bit, mis. GTA SA 2.10)  <- build utama
//   - armeabi-v7a (32-bit, mis. GTA SA 2.00) tetap didukung lewat #if di bawah
// Kode orisinal. BELUM DIUJI di perangkat: semua symbol dicek null, kalau ada yang
// tidak ditemukan plugin menonaktifkan diri dan menulis alasannya ke logcat (tag: DropMag).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <string>

MYMOD(net.dropmag.plugin, DropMag, 1.0, DropMag)
BEGIN_DEPLIST()
    ADD_DEPENDENCY_VER(net.rusjj.aml, 1.0.2.1)
END_DEPLIST()

struct V3 { float x, y, z; };

// ---------------------------------------------------------------- konfigurasi
struct Config {
    int   modelFor[48];          // indeks = eWeaponType, 0 = nonaktif
    int   spawnDelayMs = 700;
    int   lifetimeMs   = 30000;
    int   maxMags      = 20;
    float groundOffset = 0.03f;
    float gravity      = 30.0f;  // m/s^2 (efek jatuh dibuat plugin sendiri)
    float volume       = 1.0f;
    Config() { memset(modelFor, 0, sizeof(modelFor)); }
} cfg;

enum { W_PISTOL = 22, W_SILENCED = 23, W_DEAGLE = 24, W_UZI = 28, W_MP5 = 29,
       W_AK47 = 30, W_M4 = 31, W_TEC9 = 32 };

static std::string g_dir;   // folder data plugin

static void LoadConfig() {
    std::string path = g_dir + "/DropMag.ini";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) { logger->Error("DropMag: %s tidak ditemukan, pakai default (semua senjata nonaktif)", path.c_str()); return; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == ';' || line[0] == '[' || line[0] == '#') continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = line;
        float v = (float)atof(eq + 1);
        if      (!strcmp(k, "ModelPistol"))   cfg.modelFor[W_PISTOL]   = (int)v;
        else if (!strcmp(k, "ModelSilenced")) cfg.modelFor[W_SILENCED] = (int)v;
        else if (!strcmp(k, "ModelDeagle"))   cfg.modelFor[W_DEAGLE]   = (int)v;
        else if (!strcmp(k, "ModelUzi"))      cfg.modelFor[W_UZI]      = (int)v;
        else if (!strcmp(k, "ModelMP5"))      cfg.modelFor[W_MP5]      = (int)v;
        else if (!strcmp(k, "ModelAK47"))     cfg.modelFor[W_AK47]     = (int)v;
        else if (!strcmp(k, "ModelM4"))       cfg.modelFor[W_M4]       = (int)v;
        else if (!strcmp(k, "ModelTec9"))     cfg.modelFor[W_TEC9]     = (int)v;
        else if (!strcmp(k, "SpawnDelayMs"))  cfg.spawnDelayMs = (int)v;
        else if (!strcmp(k, "LifetimeMs"))    cfg.lifetimeMs   = (int)v;
        else if (!strcmp(k, "MaxMags"))       cfg.maxMags      = (int)v;
        else if (!strcmp(k, "GroundOffset"))  cfg.groundOffset = v;
        else if (!strcmp(k, "Gravity"))       cfg.gravity      = v;
        else if (!strcmp(k, "Volume"))        cfg.volume       = v;
    }
    fclose(f);
}

// ---------------------------------------------------------------- suara (OpenSL ES)
struct Sound {
    std::vector<char> pcm;
    SLObjectItf playerObj = nullptr;
    SLPlayItf   play = nullptr;
    SLAndroidSimpleBufferQueueItf bq = nullptr;
};
static SLObjectItf g_engineObj = nullptr, g_mixObj = nullptr;
static SLEngineItf g_engine = nullptr;
static Sound g_sounds[3];
static int   g_soundCount = 0;

static bool InitSound() {
    if (slCreateEngine(&g_engineObj, 0, nullptr, 0, nullptr, nullptr) != SL_RESULT_SUCCESS) return false;
    (*g_engineObj)->Realize(g_engineObj, SL_BOOLEAN_FALSE);
    (*g_engineObj)->GetInterface(g_engineObj, SL_IID_ENGINE, &g_engine);
    (*g_engine)->CreateOutputMix(g_engine, &g_mixObj, 0, nullptr, nullptr);
    (*g_mixObj)->Realize(g_mixObj, SL_BOOLEAN_FALSE);

    for (int i = 0; i < 3; i++) {
        char p[512];
        snprintf(p, sizeof(p), "%s/audio/magdrop%d.pcm", g_dir.c_str(), i + 1);
        FILE* f = fopen(p, "rb");
        if (!f) { logger->Error("DropMag: tidak bisa buka %s", p); continue; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        Sound& s = g_sounds[g_soundCount];
        s.pcm.resize(n);
        fread(s.pcm.data(), 1, n, f);
        fclose(f);

        SLDataLocator_AndroidSimpleBufferQueue q = { SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 };
        SLDataFormat_PCM fmt = { SL_DATAFORMAT_PCM, 1, SL_SAMPLINGRATE_22_05,
                                 SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                                 SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN };
        SLDataSource src = { &q, &fmt };
        SLDataLocator_OutputMix om = { SL_DATALOCATOR_OUTPUTMIX, g_mixObj };
        SLDataSink snk = { &om, nullptr };
        const SLInterfaceID ids[1] = { SL_IID_ANDROIDSIMPLEBUFFERQUEUE };
        const SLboolean req[1] = { SL_BOOLEAN_TRUE };
        if ((*g_engine)->CreateAudioPlayer(g_engine, &s.playerObj, &src, &snk, 1, ids, req) != SL_RESULT_SUCCESS) continue;
        (*s.playerObj)->Realize(s.playerObj, SL_BOOLEAN_FALSE);
        (*s.playerObj)->GetInterface(s.playerObj, SL_IID_PLAY, &s.play);
        (*s.playerObj)->GetInterface(s.playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &s.bq);
        if (cfg.volume < 1.0f) {
            SLVolumeItf vol;
            if ((*s.playerObj)->GetInterface(s.playerObj, SL_IID_VOLUME, &vol) == SL_RESULT_SUCCESS) {
                float v = cfg.volume < 0.01f ? 0.01f : cfg.volume;
                (*vol)->SetVolumeLevel(vol, (SLmillibel)(2000.0f * log10f(v)));
            }
        }
        g_soundCount++;
    }
    return g_soundCount > 0;
}

static void PlayDropSound() {
    if (!g_soundCount) return;
    Sound& s = g_sounds[rand() % g_soundCount];
    (*s.play)->SetPlayState(s.play, SL_PLAYSTATE_PLAYING);
    (*s.bq)->Clear(s.bq);
    (*s.bq)->Enqueue(s.bq, s.pcm.data(), (SLuint32)s.pcm.size());
}

// ---------------------------------------------------------------- symbol game
typedef void* (*FindPlayerPed_t)(int);
typedef void  (*GetBonePosition_t)(void* ped, V3* out, unsigned int bone, bool updateSkin);
typedef void* (*ObjectCreate_t)(int model, bool unk);
typedef void  (*WorldAddRemove_t)(void* entity);
typedef void  (*RequestModel_t)(int model, int flags);
typedef void  (*LoadAll_t)(bool);
typedef bool  (*Reload_t)(void* weapon, void* ped);
typedef void  (*TimerUpdate_t)();
typedef void  (*EntityUpdateRW_t)(void* entity);

static FindPlayerPed_t   FindPlayerPed;
static GetBonePosition_t GetBonePosition;
static ObjectCreate_t    ObjectCreate;
static WorldAddRemove_t  WorldAdd, WorldRemove;
static RequestModel_t    RequestModel;
static LoadAll_t         LoadAllRequestedModels;
static EntityUpdateRW_t  EntityUpdateRW;
static void**            ModelInfoPtrs;     // CModelInfo::ms_modelInfoPtrs
static Reload_t          Reload_orig;
static TimerUpdate_t     TimerUpdate_orig;

// Layout CPlaceable/CEntity. Pointer 8 byte di 64-bit menggeser offset.
//   32-bit: vtable(4) pos(+4) m_pMatrix(+0x14) m_pRwObject(+0x18) m_nFlags(+0x1C)
//   64-bit: vtable(8) pos(+8) m_pMatrix(+0x18) m_pRwObject(+0x20) m_nFlags(+0x28)
#if defined(__LP64__)
static const int OFF_PLACEMENT_POS = 0x08;
static const int OFF_MATRIX_PTR    = 0x18;
static const int OFF_ENT_FLAGS     = 0x28;   // bit0 = bUsesCollision
#else
static const int OFF_PLACEMENT_POS = 0x04;
static const int OFF_MATRIX_PTR    = 0x14;
static const int OFF_ENT_FLAGS     = 0x1C;
#endif
// CMatrix: right(0x00) fwd(0x10) up(0x20) pos(0x30), sama di 32/64-bit
static const int MAT_RIGHT = 0x00, MAT_FWD = 0x10, MAT_UP = 0x20, MAT_POS = 0x30;

static V3 GetPos(void* e) {
    char* p = (char*)e;
    char* m = *(char**)(p + OFF_MATRIX_PTR);
    V3* v = m ? (V3*)(m + MAT_POS) : (V3*)(p + OFF_PLACEMENT_POS);
    return *v;
}
static void SetPosRot(void* e, const V3& pos, float yaw) {
    char* p = (char*)e;
    char* m = *(char**)(p + OFF_MATRIX_PTR);
    if (!m) { *(V3*)(p + OFF_PLACEMENT_POS) = pos; return; }
    float c = cosf(yaw), s = sinf(yaw);
    *(V3*)(m + MAT_RIGHT) = { c, s, 0 };
    *(V3*)(m + MAT_FWD)   = { -s, c, 0 };
    *(V3*)(m + MAT_UP)    = { 0, 0, 1 };
    *(V3*)(m + MAT_POS)   = pos;
}

// ---------------------------------------------------------------- logika
struct Mag { void* obj; V3 pos; float yaw, vz, groundZ; bool landed; double dieAt; };
static std::vector<Mag> g_mags;
static double g_prevTime = 0, g_lastSpawn = -10;
static bool   g_pending = false;
static double g_spawnAt = 0;
static int    g_pendingModel = 0;

static double Now() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void DeleteEntity(void* e) {
    WorldRemove(e);
    // virtual deleting destructor = vtable[1] (Itanium ABI)
    void** vt = *(void***)e;
    ((void(*)(void*))vt[1])(e);
}

static void SpawnMag(void* ped, int model) {
    if (!ModelInfoPtrs || model <= 0 || model >= 20000 || !ModelInfoPtrs[model]) {
        logger->Error("DropMag: model id %d tidak valid / belum terdaftar", model);
        return;
    }
    RequestModel(model, 2 /*GAME_REQUIRED*/);
    LoadAllRequestedModels(false);

    while ((int)g_mags.size() >= cfg.maxMags && !g_mags.empty()) {
        DeleteEntity(g_mags.front().obj);
        g_mags.erase(g_mags.begin());
    }

    V3 pos = GetPos(ped);
    pos.z += 0.3f;
    if (GetBonePosition) {
        V3 b;
        GetBonePosition(ped, &b, 32 /*BONE_L_HAND*/, false);
        V3 pp = GetPos(ped);
        float dx = b.x - pp.x, dy = b.y - pp.y, dz = b.z - pp.z;
        if (dx*dx + dy*dy + dz*dz < 9.0f) pos = b;
    }

    void* obj = ObjectCreate(model, false);
    if (!obj) return;
    *(unsigned char*)((char*)obj + OFF_ENT_FLAGS) &= ~1; // tanpa collision, gerak diatur plugin

    Mag m;
    m.obj = obj; m.pos = pos; m.vz = 0;
    m.yaw = (rand() % 628) / 100.0f;
    m.groundZ = GetPos(ped).z - 1.0f + cfg.groundOffset; // kira-kira tinggi tanah di bawah kaki ped
    m.landed = false;
    m.dieAt = Now() + cfg.lifetimeMs / 1000.0;
    SetPosRot(obj, pos, m.yaw);
    WorldAdd(obj);
    if (EntityUpdateRW) EntityUpdateRW(obj);
    g_mags.push_back(m);
}

static void UpdateMags(float dt) {
    double now = Now();
    for (size_t i = 0; i < g_mags.size();) {
        Mag& m = g_mags[i];
        if (now >= m.dieAt) { DeleteEntity(m.obj); g_mags.erase(g_mags.begin() + i); continue; }
        if (!m.landed) {
            m.vz -= cfg.gravity * dt;
            m.pos.z += m.vz * dt;
            bool land = m.pos.z <= m.groundZ;
            if (land) m.pos.z = m.groundZ;
            WorldRemove(m.obj);
            SetPosRot(m.obj, m.pos, m.yaw);
            WorldAdd(m.obj);
            if (EntityUpdateRW) EntityUpdateRW(m.obj);
            if (land) { m.landed = true; PlayDropSound(); }
        }
        ++i;
    }
}

// ---------------------------------------------------------------- hook
// CWeapon: +0 tipe senjata, +4 state (2 = RELOADING)
static bool Reload_hook(void* weapon, void* ped) {
    int stateBefore = *(int*)((char*)weapon + 4);
    bool r = Reload_orig(weapon, ped);
    int stateAfter = *(int*)((char*)weapon + 4);
    int type = *(int*)weapon;

    if (stateBefore != 2 && stateAfter == 2 &&
        FindPlayerPed && ped == FindPlayerPed(-1) &&
        type >= 0 && type < 48 && cfg.modelFor[type] > 0) {
        double now = Now();
        if (now - g_lastSpawn > 1.0) {
            g_lastSpawn = now;
            g_pending = true;
            g_pendingModel = cfg.modelFor[type];
            g_spawnAt = now + cfg.spawnDelayMs / 1000.0;
        }
    }
    return r;
}

static void Tick_hook() {
    TimerUpdate_orig();
    double now = Now();
    float dt = g_prevTime > 0 ? (float)(now - g_prevTime) : 0.016f;
    if (dt > 0.1f) dt = 0.1f;
    g_prevTime = now;

    if (g_pending && now >= g_spawnAt) {
        g_pending = false;
        void* ped = FindPlayerPed(-1);
        if (ped) SpawnMag(ped, g_pendingModel);
    }
    UpdateMags(dt);
}

// ---------------------------------------------------------------- entry
extern "C" void OnModLoad() {
    logger->SetTag("DropMag");
    g_dir = std::string(aml->GetConfigPath()) + "/DropMag";
    LoadConfig();

    void* h = aml->GetLibHandle("libGTASA.so");
    if (!h) { logger->Error("DropMag: libGTASA.so tidak ketemu"); return; }
    #define SYM(var, name) var = (decltype(var))aml->GetSym(h, name); \
        if (!var) logger->Error("DropMag: symbol hilang: %s", name)
    SYM(FindPlayerPed,          "_Z13FindPlayerPedi");
    SYM(GetBonePosition,        "_ZN4CPed15GetBonePositionER5RwV3djb");
    SYM(ObjectCreate,           "_ZN7CObject6CreateEib");
    SYM(WorldAdd,               "_ZN6CWorld3AddEP7CEntity");
    SYM(WorldRemove,            "_ZN6CWorld6RemoveEP7CEntity");
    SYM(RequestModel,           "_ZN10CStreaming12RequestModelEii");
    SYM(LoadAllRequestedModels, "_ZN10CStreaming22LoadAllRequestedModelsEb");
    SYM(EntityUpdateRW,         "_ZN7CEntity8UpdateRWEv");
    SYM(ModelInfoPtrs,          "_ZN10CModelInfo16ms_modelInfoPtrsE");
    uintptr_t reloadFn = aml->GetSym(h, "_ZN7CWeapon6ReloadEP4CPed");
    uintptr_t tickFn   = aml->GetSym(h, "_ZN6CTimer6UpdateEv");

    if (!FindPlayerPed || !ObjectCreate || !WorldAdd || !WorldRemove || !RequestModel ||
        !LoadAllRequestedModels || !ModelInfoPtrs || !reloadFn || !tickFn) {
        logger->Error("DropMag: symbol penting hilang, plugin dinonaktifkan");
        return;
    }
    if (!InitSound()) logger->Error("DropMag: suara tidak aktif (cek folder audio/*.pcm)");

    aml->Hook(reloadFn, (void*)Reload_hook, (void**)&Reload_orig);
    aml->Hook(tickFn,   (void*)Tick_hook,   (void**)&TimerUpdate_orig);
    logger->Info("DropMag aktif");
}
