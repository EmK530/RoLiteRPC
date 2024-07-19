#include <iostream>
#include <csignal>
#include <thread>

#include <codecvt>

#include <Windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <curl/curl.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "discord-files/discord.h"

typedef NTSTATUS(WINAPI* pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
    );

HMODULE hNtDll;
pfnNtQueryInformationProcess ntqueryfunc;

const wchar_t* widen(const char* c)
{
    size_t size = strlen(c) + 1;
    wchar_t* wider = new wchar_t[size];
    size_t outSize;
    mbstowcs_s(&outSize, wider, size, c, size - 1);
    return wider;
}

void easy_error(const char* input) {
    //std::cout << "[ERROR] " << input;
    MessageBox(0, widen(input), L"Fatal error", MB_ICONERROR);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    std::exit(-1);
}

bool GetProcessCommandLine(DWORD processId, std::wstring& imageName) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!hProcess) {
        std::cerr << "Failed to open process" << std::endl;
        MessageBox(0, L"Could not perform OpenProcess!\nYou may need to run RoLiteRPC as administrator.", L"Can't read place!", MB_ICONWARNING);
        FreeLibrary(hNtDll);
        return false;
    }
    ULONG size = 8192;
    auto buffer = std::make_unique<BYTE[]>(size);
    auto status = ntqueryfunc(hProcess, (PROCESSINFOCLASS)60, buffer.get(), size, &size);
    if (status != 0x00000000) { // Check for NT_SUCCESS macro
        std::cerr << "NtQueryInformationProcess failed with status 0x" << std::hex << status << std::endl;
        MessageBox(0, L"NtQueryInformationProcess failed, please open an issue report about this!", L"Can't read place!", MB_ICONWARNING);
        CloseHandle(hProcess);
        FreeLibrary(hNtDll);
        return false;
    }
    auto str = (UNICODE_STRING*)buffer.get();
    imageName = std::wstring(str->Buffer, str->Length / sizeof(WCHAR));
    CloseHandle(hProcess);
    FreeLibrary(hNtDll);
    return true;
}

DWORD GetPIDFromName(std::wstring input, bool ignoreSelf = false) {
    DWORD self = _getpid();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create snapshot of processes.\n";
        return 0;
    }
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hSnapshot, &pe32)) {
        CloseHandle(hSnapshot);
        std::cerr << "Failed to retrieve information about the first process.\n";
        return 0;
    }
    do {
        std::wstring currentProcessName = pe32.szExeFile;
        if (currentProcessName == input && (!ignoreSelf || pe32.th32ProcessID != self)) {
            return pe32.th32ProcessID;
        }
    } while (Process32Next(hSnapshot, &pe32));
    CloseHandle(hSnapshot);
    return 0;
}

bool GetPlaceIdFromCommandLine(std::wstring commandLine, std::wstring& output)
{
    size_t found = commandLine.find(L"%26placeId%3D");
    std::wstring placeId;
    if (found == std::string::npos)
    {
        found = commandLine.find(L"&placeId=");
        if (found == std::string::npos)
        {
            return false;
        }
        else {
            placeId = commandLine.substr(found + 9);
            placeId = placeId.substr(0, placeId.find(L"&"));
        }
    }
    else {
        placeId = commandLine.substr(found + 13);
        placeId = placeId.substr(0, placeId.find(L"%26"));
    }
    output = placeId;
    return true;
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

discord::Activity curActivity;

bool SetupAllInfoFromPlaceID(std::wstring WplaceId)
{
    std::cout << "Getting universe ID...\n";

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::string AplaceId = converter.to_bytes(WplaceId);

    json findUniverse;
    CURL* curl = curl_easy_init();
    if (curl) {
        std::string response;
        std::string url = "https://apis.roblox.com/universes/v1/places/" + AplaceId + "/universe";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Could not get universe ID from place ID: " << curl_easy_strerror(res) << std::endl;
            return false;
        } else {
            findUniverse = json::parse(response);
        }
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Could not prepare cURL request #1";
        return false;
    }

    int64_t universe = findUniverse["universeId"].get<int64_t>();
    std::cout << "Success: " << universe << "\nGetting universe info...\n";

    json universeInfo;
    curl = curl_easy_init();
    if (curl) {
        std::string response;
        std::string url = "https://games.roblox.com/v1/games?universeIds=" + std::to_string(universe);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Could not get universe info: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        else {
            universeInfo = json::parse(response);
        }
        curl_easy_cleanup(curl);
    } else {
        std::cerr << "Could not prepare cURL request #2";
        return false;
    }

    std::cout << "Success, getting game icon...\n";
    json thumbnailInfo;
    curl = curl_easy_init();
    if (curl) {
        std::string response;
        std::string url = "https://thumbnails.roblox.com/v1/games/icons?universeIds=" + std::to_string(universe) + "&returnPolicy=PlaceHolder&size=512x512&format=Png&isCircular=false";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "Could not get game icon: " << curl_easy_strerror(res) << std::endl;
            return false;
        }
        else {
            thumbnailInfo = json::parse(response);
        }
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "Could not prepare cURL request #3";
        return false;
    }

    auto game = universeInfo["data"][0];
    std::string GameName = game["name"].get<std::string>();
    std::string CreatorName = game["creator"]["name"].get<std::string>();
    std::string GameIcon = thumbnailInfo["data"][0]["imageUrl"].get<std::string>();
    discord::Activity activity{};
    std::string det = "Playing " + GameName;
    activity.SetDetails(det.c_str());
    std::string by = "by " + CreatorName;
    activity.SetState(by.c_str());
    activity.GetAssets().SetSmallImage("roblox");
    activity.GetAssets().SetSmallText("Roblox");
    activity.GetAssets().SetLargeImage(GameIcon.c_str());
    activity.GetAssets().SetLargeText(GameName.c_str());
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    activity.GetTimestamps().SetStart(now_time_t);
    activity.SetType(discord::ActivityType::Playing);
    curActivity = activity;

    std::cout << "Success! Creating activity for: " << GameName << "\n";

    return true;
}

int main()
{
    LPSTR buf = new char[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buf);
    std::string folderName = buf;
    folderName = folderName.substr(folderName.find_last_of("\\") + 1);

    //setup
    HMODULE hNtDll = LoadLibraryA("ntdll.dll");
    if (!hNtDll) {
        easy_error("Failed to load ntdll.dll");
    } else {
        ntqueryfunc = reinterpret_cast<pfnNtQueryInformationProcess>(GetProcAddress(hNtDll, "NtQueryInformationProcess"));
        if (!ntqueryfunc) {
            FreeLibrary(hNtDll);
            easy_error("Failed to load NtQueryInformationProcess");
        }
    }

    if (GetPIDFromName(L"RoLiteRPC.exe", true) != 0) {
        MessageBox(0, L"An instance of RoLiteRPC is already open.", L"Multi-instance alert!", MB_ICONWARNING);
        std::exit(-1);
    }

    if (folderName != "Startup") {
        MessageBox(0, L"RoLiteRPC is now active!\n\nFelt I had to inform, otherwise it would just look like nothing happened when you launched the app!\nIf you want to close this, run the batch file that came with the release.", L"Information", MB_ICONINFORMATION);
    }

    //begin code
    DWORD lastCheck = 0;

    bool gameLoaded = false;
    bool RPCLoaded = false;
    bool DiscordLoaded = false;
    bool firstrun = true;
    discord::Core* core{};

    while (true) {
        DWORD pid = GetPIDFromName(L"RobloxPlayerBeta.exe");
        if (pid != 0) {
            if (pid != lastCheck) {
                std::cout << "Detected new Roblox instance.\n";
                std::wstring commandLine;
                if (GetProcessCommandLine(pid, commandLine)) {
                    std::wstring placeId;
                    if (GetPlaceIdFromCommandLine(commandLine, placeId)) {
                        std::wcout << L"You are currently in place ID: " << placeId << std::endl;
                        gameLoaded = SetupAllInfoFromPlaceID(placeId);
                    }
                    else {
                        std::cerr << "Failed to find place ID in Roblox's command line.\n";
                        MessageBox(0, L"RoLiteRPC does not support launching outside of web!\nYour activity won't display properly.", L"Can't read place!", MB_ICONWARNING);
                    }
                }
                else {
                    std::cerr << "Failed to retrieve command line for Roblox.\n";
                }
                lastCheck = pid;
            }
        }
        else {
            // roblox is not running
            if (firstrun)
            {
                std::cout << "Roblox is not running, waiting...\n";
            }
            if (gameLoaded)
            {
                std::cout << "Roblox has closed!\n";
            }
            lastCheck = 0;
            gameLoaded = false;
        }
        if ((!core || !DiscordLoaded) && gameLoaded) {
            std::cout << "We require discord now, time to load it!\n";
            auto response = discord::Core::Create(1262917347069394984, DiscordCreateFlags_NoRequireDiscord, &core);
            if (!core) {
                std::cerr << "Could not create Discord core.\n";
            }
            else {
                std::cout << "Connected to Discord.\n";
                DiscordLoaded = true;
            }
        }
        if (core) {
            if (!gameLoaded && RPCLoaded) {
                std::cout << "Clearing Discord activity... by disconnecting.\n";
                delete core;
                core = nullptr;
                DiscordLoaded = false;
                RPCLoaded = false;
            }
            else if (!RPCLoaded && gameLoaded)
            {
                std::cout << "Attempting to apply activity...\n";
                core->ActivityManager().UpdateActivity(curActivity, [](discord::Result result) {
                    std::cout << ((result == discord::Result::Ok) ? "Succeeded in updating " : "Failed to update ")
                        << "activity!\n";
                    });
                RPCLoaded = true;
            }
        }
        for (int i = 0; i < 100; i++) {
            if (core && DiscordLoaded) {
                core->RunCallbacks();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        firstrun = false;
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    main();
}
