// Made By: Sander Kerkhoff

#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <Lmcons.h>
#include <winternl.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#pragma comment(lib, "pdh.lib")

// History buffers for graphing
std::vector<float> cpuHistory(100, 0.0f);
std::vector<float> ramHistory(100, 0.0f);

// PDH handles
PDH_HQUERY cpuQuery;
PDH_HCOUNTER cpuCounter;

// Utility: Convert wide string to regular string
std::string WStringToString(const std::wstring& wstr) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), result.data(), size, NULL, NULL);
    return result;
}

// Get formatted system uptime as HH:MM:SS
std::string GetSystemUptime() {
    DWORD64 seconds = GetTickCount64() / 1000;
    DWORD h = (DWORD)(seconds / 3600);
    DWORD m = (seconds % 3600) / 60;
    DWORD s = seconds % 60;
    char buffer[64];
    sprintf_s(buffer, "%02lu:%02lu:%02lu", h, m, s);
    return buffer;
}

// Retrieve Windows version
typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
std::string GetOSVersion() {
    HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
    if (!hMod) return "Unknown";
    RtlGetVersionPtr fx = (RtlGetVersionPtr)::GetProcAddress(hMod, "RtlGetVersion");
    if (!fx) return "Unknown";

    RTL_OSVERSIONINFOW rovi = { 0 };
    rovi.dwOSVersionInfoSize = sizeof(rovi);
    if (fx(&rovi) != 0) return "Unknown";

    return "Windows " + std::to_string(rovi.dwMajorVersion) + "." + std::to_string(rovi.dwMinorVersion) + " (Build " + std::to_string(rovi.dwBuildNumber) + ")";
}

// Get user and computer names
std::string GetUsername() {
    TCHAR user[UNLEN + 1];
    DWORD size = UNLEN + 1;
    GetUserName(user, &size);
    return WStringToString(user);
}

std::string GetComputerNameStr() {
    TCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerName(name, &size);
    return WStringToString(name);
}

// System usage info
float GetCPUUsage() {
    PDH_FMT_COUNTERVALUE val;
    PdhCollectQueryData(cpuQuery);
    if (PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL, &val) == ERROR_SUCCESS) {
        return static_cast<float>(val.doubleValue);
    }
    return 0.0f;
}

float GetRAMUsage() {
    MEMORYSTATUSEX mem = { sizeof(mem) };
    GlobalMemoryStatusEx(&mem);
    return 100.0f * (mem.ullTotalPhys - mem.ullAvailPhys) / mem.ullTotalPhys;
}


// Move graph data buffer forward
void PushHistory(std::vector<float>& history, float value) {
    history.erase(history.begin());
    history.push_back(value);
}

int main() {
    // Initialize CPU usage counter
    if (PdhOpenQuery(NULL, NULL, &cpuQuery) != ERROR_SUCCESS ||
        PdhAddCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuCounter) != ERROR_SUCCESS) {
        MessageBoxA(NULL, "Failed to initialize CPU counter", "Error", MB_OK);
        return -1;
    }

    // Init GLFW + OpenGL + ImGui
    glfwInit();
    const char* glsl_version = "#version 130";
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Hardware Profiler", NULL, NULL);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Render loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::Begin("Profiler", NULL, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        float cpu = GetCPUUsage();
        float ram = GetRAMUsage();

        PushHistory(cpuHistory, cpu);
        PushHistory(ramHistory, ram);

        ImGui::Text("System Information");
        ImGui::Text("Operating System: %s", GetOSVersion().c_str());
        ImGui::Text("Username: %s", GetUsername().c_str());
        ImGui::Text("Computer Name: %s", GetComputerNameStr().c_str());
        ImGui::Text("System Uptime: %s", GetSystemUptime().c_str());

        ImGui::Separator();
        ImGui::Text("CPU Usage: %.1f%%", cpu);
        ImGui::PlotLines("##CPU", cpuHistory.data(), (int)cpuHistory.size(), 0, NULL, 0.0f, 100.0f, ImVec2(0, 80));

        ImGui::Text("RAM Usage: %.1f%%", ram);
        ImGui::PlotLines("##RAM", ramHistory.data(), (int)ramHistory.size(), 0, NULL, 0.0f, 100.0f, ImVec2(0, 80));

        ImGui::End();

        ImGui::Render();
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}