#include <switch.h>
#include <nanovg_dk.h>
#include <nanovg/framework/CMemPool.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <optional>
#include <vector>
#include <cstdio>
#include "main.h"

namespace {

constexpr unsigned Width = 1280, Height = 720;

namespace Theme {
const NVGcolor Background = nvgRGB(17, 31, 40);
const NVGcolor Panel = nvgRGB(18, 36, 48);
const NVGcolor Selected = nvgRGB(20, 49, 68);
const NVGcolor Line = nvgRGB(51, 94, 119);
const NVGcolor Foreground = nvgRGB(251, 251, 251);
const NVGcolor Muted = nvgRGB(190, 208, 214);
const NVGcolor Accent = nvgRGB(50, 255, 207);
const NVGcolor Blocked = nvgRGB(105, 255, 143);
const NVGcolor Error = nvgRGB(250, 58, 93);
const NVGcolor Warning = nvgRGB(255, 202, 105);
}

struct Row {
    RESOLVER_STATUS legacy = DNS_PENDING;
    RESOLVER_STATUS async = DNS_PENDING;
};

class DnsRun {
    Thread thread{};
    bool created = false;
    Mutex mutex{};
    std::vector<Row> rows;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    std::atomic<int> logStatus{0};

    static bool Progress(void *context, unsigned index, RESOLVER_STATUS legacy, RESOLVER_STATUS async) {
        auto &self = *static_cast<DnsRun *>(context);
        if (index == self.rows.size()) {
            self.logStatus.store(legacy == DNS_ERROR ? -1 : 1);
        } else {
            mutexLock(&self.mutex);
            self.rows[index] = {legacy, async};
            mutexUnlock(&self.mutex);
        }
        return !self.stop.load();
    }

    static void Worker(void *context) {
        auto &self = *static_cast<DnsRun *>(context);
        checkHostnames(Progress, context);
        self.running.store(false);
    }

public:
    DnsRun() : rows(hostnameCount()) { mutexInit(&mutex); }
    ~DnsRun() { Join(); }
    bool Running() const { return running.load(); }
    int LogStatus() const { return logStatus.load(); }
    void Stop() { stop.store(true); }
    void Join() {
        if (created) {
            Stop();
            threadWaitForExit(&thread);
            threadClose(&thread);
            created = false;
        }
    }
    Result Start() {
        if (Running()) return 0;
        Join();
        std::fill(rows.begin(), rows.end(), Row{});
        stop.store(false);
        logStatus.store(0);
        Result rc = threadCreate(&thread, Worker, this, nullptr, 0x10000, 0x2C, -2);
        if (R_FAILED(rc)) return rc;
        created = true;
        running.store(true);
        rc = threadStart(&thread);
        if (R_FAILED(rc)) {
            running.store(false);
            threadClose(&thread);
            created = false;
        }
        return rc;
    }
    std::vector<Row> Snapshot() {
        mutexLock(&mutex);
        auto copy = rows;
        mutexUnlock(&mutex);
        return copy;
    }
};

void Rectangle(NVGcontext *vg, float x, float y, float w, float h, NVGcolor color, float radius = 4) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void Text(NVGcontext *vg, float x, float y, const char *text, float size = 20, NVGcolor color = Theme::Foreground) {
    nvgFontFace(vg, "switch-standard");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, text, nullptr);
}

void Status(NVGcontext *vg, float x, float y, RESOLVER_STATUS status) {
    static const char *names[] = {"Blocked", "Unblocked", "Unresolved", "Timeout", "Offline", "Error", "Timeout", "Pending"};
    NVGcolor color = status == DNS_BLOCKED ? Theme::Blocked :
        status == DNS_RESOLVED ? Theme::Error : status == DNS_PENDING ? Theme::Muted : Theme::Warning;
    Text(vg, x, y, names[status], 16, color);
}

class Display {
    dk::UniqueDevice device;
    dk::UniqueQueue queue;
    dk::UniqueCmdBuf commands;
    dk::UniqueSwapchain swapchain;
    std::optional<CMemPool> images, code, data;
    CMemPool::Handle commandMemory, stencilMemory;
    std::array<CMemPool::Handle, 2> framebufferMemory;
    dk::Image stencil;
    std::array<dk::Image, 2> framebuffers;
    std::optional<nvg::DkRenderer> renderer;
    NVGcontext *vg = nullptr;

public:
    Display() {
        device = dk::DeviceMaker{}.create();
        queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();
        images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 24*1024*1024);
        code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128*1024);
        data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1024*1024);
        commands = dk::CmdBufMaker{device}.create();
        commandMemory = data->allocate(0x1000);
        commands.addMemory(commandMemory.getMemBlock(), commandMemory.getOffset(), commandMemory.getSize());

        dk::ImageLayout layout;
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_S8).setDimensions(Width, Height).initialize(layout);
        stencilMemory = images->allocate(layout.getSize(), layout.getAlignment());
        stencil.initialize(layout, stencilMemory.getMemBlock(), stencilMemory.getOffset());
        dk::ImageLayoutMaker{device}.setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
            .setFormat(DkImageFormat_RGBA8_Unorm).setDimensions(Width, Height).initialize(layout);
        std::array<DkImage const *, 2> targets;
        for (unsigned i = 0; i < framebuffers.size(); i++) {
            framebufferMemory[i] = images->allocate(layout.getSize(), layout.getAlignment());
            framebuffers[i].initialize(layout, framebufferMemory[i].getMemBlock(), framebufferMemory[i].getOffset());
            targets[i] = &framebuffers[i];
        }
        swapchain = dk::SwapchainMaker{device, nwindowGetDefault(), targets}.create();
        renderer.emplace(Width, Height, device, queue, *images, *code, *data);
        vg = nvgCreateDk(&*renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!vg) fatalThrow(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
        PlFontData font{};
        Result rc = plGetSharedFontByType(&font, PlSharedFontType_Standard);
        if (R_FAILED(rc)) fatalThrow(rc);
        if (nvgCreateFontMem(vg, "switch-standard", static_cast<u8 *>(font.address), font.size, 0) < 0)
            fatalThrow(MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
    }
    ~Display() {
        queue.waitIdle();
        nvgDeleteDk(vg);
        renderer.reset();
        commands.clear();
        swapchain.destroy();
        commands.destroy();
        commandMemory.destroy();
        for (auto &memory : framebufferMemory) memory.destroy();
        stencilMemory.destroy();
    }

    void Draw(const std::vector<Row> &rows, bool running, bool stopping, int logStatus, Result error) {
        queue.waitIdle();
        commands.clear();
        const int slot = queue.acquireImage(swapchain);
        dk::ImageView color{framebuffers[slot]}, depth{stencil};
        commands.bindRenderTargets(&color, &depth);
        commands.setViewports(0, {{0, 0, Width, Height, 0, 1}});
        commands.setScissors(0, {{0, 0, Width, Height}});
        commands.clearColor(0, DkColorMask_RGBA, 0, 0, 0, 1);
        commands.clearDepthStencil(true, 1, 0xFF, 0);
        dk::RasterizerState rasterizer;
        dk::ColorState colors;
        dk::ColorWriteState writes;
        commands.bindRasterizerState(rasterizer);
        commands.bindColorState(colors);
        commands.bindColorWriteState(writes);
        queue.submitCommands(commands.finishList());
        nvgBeginFrame(vg, Width, Height, 1);
        Rectangle(vg, 0, 0, Width, Height, Theme::Background, 0);
        Rectangle(vg, 55, 49, 4, 30, Theme::Accent, 0);
        Text(vg, 75, 48, "90DNS Testing Utility", 30);
        Rectangle(vg, 55, 114, 1170, 1, Theme::Line, 0);
        Rectangle(vg, 55, 630, 1170, 1, Theme::Line, 0);
        char info[256];
        u32 version = hosversionGet();
        bool asyncSupported = hosversionAtLeast(23, 0, 0);
        snprintf(info, sizeof(info), "v1.0.6  |  FW %u.%u.%u  |  Legacy + c-ares%s", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version), asyncSupported ? "" : " (FW 23+ required)");
        Text(vg, 75, 89, info, 18, Theme::Muted);

        unsigned completed = 0, legacyBlocked = 0, asyncBlocked = 0;
        unsigned legacyUnblocked = 0, asyncUnblocked = 0, legacyUnknown = 0, asyncUnknown = 0;
        for (const auto &row : rows) {
            completed += row.legacy != DNS_PENDING && (!asyncSupported || row.async != DNS_PENDING);
            legacyBlocked += row.legacy == DNS_BLOCKED;
            asyncBlocked += row.async == DNS_BLOCKED;
            legacyUnblocked += row.legacy == DNS_RESOLVED;
            asyncUnblocked += row.async == DNS_RESOLVED;
            legacyUnknown += row.legacy != DNS_PENDING && row.legacy != DNS_BLOCKED && row.legacy != DNS_RESOLVED;
            asyncUnknown += row.async != DNS_PENDING && row.async != DNS_BLOCKED && row.async != DNS_RESOLVED;
        }
        snprintf(info, sizeof(info), "%s  %u/%zu", stopping ? "Stopping..." : running ? "Checking..." : R_FAILED(error) ? "Start failed" : "Finished", completed, rows.size());
        Text(vg, 945, 89, info, 18, R_FAILED(error) ? Theme::Error : Theme::Accent);
        snprintf(info, sizeof(info), "Legacy: %u blocked / %u unblocked / %u unverified", legacyBlocked, legacyUnblocked, legacyUnknown);
        Text(vg, 75, 125, info, 18);
        if (asyncSupported)
            snprintf(info, sizeof(info), "c-ares: %u blocked / %u unblocked / %u unverified", asyncBlocked, asyncUnblocked, asyncUnknown);
        else
            snprintf(info, sizeof(info), "c-ares: N/A (requires FW 23+)");
        Text(vg, 655, 125, info, 18);
        Rectangle(vg, 75, 156, 1130, 8, nvgRGB(11, 21, 25), 4);
        if (completed) Rectangle(vg, 75, 156, 1130.f * completed / rows.size(), 8, Theme::Accent, 4);
        const unsigned rowsPerColumn = (rows.size() + 1) / 2;
        for (unsigned column = 0; column < 2; column++) {
            const float x = 75 + column * 580;
            Rectangle(vg, x, 177, 550, 26, Theme::Selected);
            Text(vg, x + 10, 182, "Domain", 16, Theme::Accent);
            Text(vg, x + 350, 182, "Legacy", 16, Theme::Accent);
            Text(vg, x + 450, 182, "c-ares", 16, Theme::Accent);
            for (unsigned row = 0; row < rowsPerColumn; row++) {
                const unsigned i = column * rowsPerColumn + row;
                if (i >= rows.size()) break;
                const float y = 209 + row * 20;
                if (row % 2 == 0) Rectangle(vg, x, y - 1, 550, 20, Theme::Panel, 2);
                Text(vg, x + 10, y, hostnameAt(i), 16);
                Status(vg, x + 350, y, rows[i].legacy);
                if (asyncSupported) Status(vg, x + 450, y, rows[i].async);
                else Text(vg, x + 450, y, "N/A", 16, Theme::Muted);
            }
        }
        Text(vg, 75, 578, "Blocked results cover these DNS queries only; they do not guarantee protection from bans.", 17, Theme::Muted);
        if (R_FAILED(error)) snprintf(info, sizeof(info), "Worker error: 0x%08X", error);
        else snprintf(info, sizeof(info), "%s", logStatus < 0 ? "Log write failed: sdmc:/90dns-test-log.txt" : logStatus > 0 ? "Log saved: sdmc:/90dns-test-log.txt" : "Log: sdmc:/90dns-test-log.txt");
        Text(vg, 75, 604, info, 17, R_FAILED(error) || logStatus < 0 ? Theme::Error : Theme::Muted);
        Rectangle(vg, 75, 640, 540, 36, running ? Theme::Panel : Theme::Selected);
        if (!running) Rectangle(vg, 75, 640, 3, 36, Theme::Accent, 0);
        Text(vg, 95, 647, running ? "Checking DNS..." : "X: Retry", 19, running ? Theme::Muted : Theme::Accent);
        Rectangle(vg, 635, 640, 570, 36, Theme::Selected);
        Text(vg, 655, 647, stopping ? "Waiting for current query to finish..." : "B / +: Exit", 19);
        nvgEndFrame(vg);
        queue.presentImage(swapchain, slot);
    }
};

}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    Result rc = romfsInit();
    if (R_FAILED(rc)) fatalThrow(rc);
    rc = plInitialize(PlServiceType_User);
    if (R_FAILED(rc)) fatalThrow(rc);
    rc = socketInitializeDefault();
    if (R_FAILED(rc)) fatalThrow(rc);
    rc = nifmInitialize(NifmServiceType_User);
    if (R_FAILED(rc)) fatalThrow(rc);
    {
        Display display;
        DnsRun run;
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadFullCtrl);
        padInitializeAny(&pad);
        hidInitializeTouchScreen();
        bool touching = false, exiting = false;
        rc = run.Start();
        while (appletMainLoop()) {
            padUpdate(&pad);
            u64 down = padGetButtonsDown(&pad);
            HidTouchScreenState touch{};
            hidGetTouchScreenStates(&touch, 1);
            bool pressed = touch.count && !touching;
            touching = touch.count != 0;
            if (pressed && touch.touches[0].y >= 640 && touch.touches[0].y <= 676) {
                if (touch.touches[0].x >= 75 && touch.touches[0].x <= 615) down |= HidNpadButton_X;
                if (touch.touches[0].x >= 635 && touch.touches[0].x <= 1205) down |= HidNpadButton_B;
            }
            if (down & (HidNpadButton_B | HidNpadButton_Plus)) {
                exiting = true;
                run.Stop();
            }
            if (exiting && !run.Running()) break;
            if (!exiting && !run.Running() && (down & HidNpadButton_X)) {
                rc = run.Start();
            }
            display.Draw(run.Snapshot(), run.Running(), exiting, run.LogStatus(), rc);
        }
        run.Join();
    }
    nifmExit();
    socketExit();
    plExit();
    romfsExit();
    return 0;
}
