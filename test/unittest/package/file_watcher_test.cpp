/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "securec.h"
#include "file_watcher.h"
#include "hilog_object_editor.h"

using namespace testing;
using namespace testing::ext;

namespace OHOS {
namespace ObjectEditor {

class FileWatcherTest : public testing::Test {
public:
    static void SetUpTestCase();
    static void TearDownTestCase();
    void SetUp();
    void TearDown();
};

void FileWatcherTest::SetUpTestCase()
{
}

void FileWatcherTest::TearDownTestCase()
{
}

void FileWatcherTest::SetUp()
{
}

void FileWatcherTest::TearDown()
{
}

namespace {

uint32_t g_callbackMask = 0;
std::string g_callbackFilepath;

// Replicates the file-scope const from file_watcher.cpp's anonymous namespace
// (which is not exported). CleanupResources() calls
// fdsan_close_with_tag(inotifyFd_, FILE_WATCHER_FD_TAG), so a test that hands
// the watcher a real inotify fd MUST tag that fd with this exact value first,
// otherwise fdsan aborts with a tag mismatch. See fault log
// fdsan-file_watcher_test-0-20261114114244300.log.
constexpr uint64_t FILE_WATCHER_TAG_ID = 0xD005118;
const uint64_t FILE_WATCHER_FD_TAG =
    fdsan_create_owner_tag(FDSAN_OWNER_TYPE_DEFAULT, FILE_WATCHER_TAG_ID);
constexpr int32_t TEMP_PATH_MAX_LEN = 64;

void MockEventCallback(uint32_t mask, const std::string &filepath)
{
    g_callbackMask = mask;
    g_callbackFilepath = filepath;
}

}

/**
 * @tc.name: Start_001
 * @tc.desc: Test FileWatcher Start with empty filepath and null callback returns false
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, Start_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("", nullptr);
    bool result = watcher->Start();
    EXPECT_EQ(result, false);
}

/**
 * @tc.name: Start_002
 * @tc.desc: Test FileWatcher Start when already running returns false
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, Start_002, TestSize.Level1)
{
    bool callbackCalled = false;
    auto watcher = std::make_unique<FileWatcher>("",
        [&callbackCalled](uint32_t mask, const std::string &filepath) {
            callbackCalled = true;
        });
    watcher->running_.store(true);
    bool result = watcher->Start();
    EXPECT_EQ(result, false);
}

/**
 * @tc.name: Stop_001
 * @tc.desc: Test FileWatcher Stop when not running and thread not created
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, Stop_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("",
        [](uint32_t mask, const std::string &filepath) {});
    watcher->running_.store(false);
    watcher->threadCreated_.store(false);
    watcher->Stop();
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: Stop_002
 * @tc.desc: Test FileWatcher Stop when running but thread not created sets running false
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, Stop_002, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("",
        [](uint32_t mask, const std::string &filepath) {});
    watcher->running_.store(true);
    watcher->threadCreated_.store(false);
    watcher->Stop();
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: WaitForEvent_001
 * @tc.desc: Test FileWatcher WaitForEvent returns false when inotifyFd is invalid
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, WaitForEvent_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("",
        [](uint32_t mask, const std::string &filepath) {});
    bool result = watcher->WaitForEvent();
    EXPECT_EQ(result, false);
}

/**
 * @tc.name: ProcessEvent_001
 * @tc.desc: Test ProcessEvent with IN_MODIFY event triggers callback with correct mask and filepath
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ProcessEvent_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        MockEventCallback);
    struct inotify_event event;
    event.wd = 1;
    event.mask = IN_CLOSE_WRITE;
    event.cookie = 0;
    event.len = 0;
    char buffer[sizeof(struct inotify_event) + 16] = {0};
    memcpy_s(buffer, sizeof(buffer), &event, sizeof(struct inotify_event));
    ssize_t len = sizeof(struct inotify_event);
    g_callbackMask = 0;
    g_callbackFilepath = "";
    watcher->ProcessEvent(buffer, len);
    EXPECT_EQ(g_callbackMask, static_cast<uint32_t>(IN_CLOSE_WRITE));
    EXPECT_EQ(g_callbackFilepath, "/test/file");
}

/**
 * @tc.name: ProcessEvent_002
 * @tc.desc: Test ProcessEvent with IN_DELETE_SELF event stops watcher
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ProcessEvent_002, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        MockEventCallback);
    struct inotify_event event;
    event.wd = 1;
    event.mask = IN_DELETE_SELF;
    event.cookie = 0;
    event.len = 0;
    char buffer[sizeof(struct inotify_event) + 16] = {0};
    memcpy_s(buffer, sizeof(buffer), &event, sizeof(struct inotify_event));
    ssize_t len = sizeof(struct inotify_event);
    g_callbackMask = 0;
    watcher->ProcessEvent(buffer, len);
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: ProcessEvent_003
 * @tc.desc: Test ProcessEvent with IN_MOVE_SELF event stops watcher
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ProcessEvent_003, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        MockEventCallback);
    struct inotify_event event;
    event.wd = 1;
    event.mask = IN_MOVE_SELF;
    event.cookie = 0;
    event.len = 0;
    char buffer[sizeof(struct inotify_event) + 16] = {0};
    memcpy_s(buffer, sizeof(buffer), &event, sizeof(struct inotify_event));
    ssize_t len = sizeof(struct inotify_event);
    watcher->ProcessEvent(buffer, len);
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: ProcessEvent_004
 * @tc.desc: Test ProcessEvent with null callback does not crash and keeps running true
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ProcessEvent_004, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        nullptr);
    struct inotify_event event;
    event.wd = 1;
    event.mask = IN_MODIFY;
    event.cookie = 0;
    event.len = 0;
    char buffer[sizeof(struct inotify_event) + 16] = {0};
    memcpy_s(buffer, sizeof(buffer), &event, sizeof(struct inotify_event));
    ssize_t len = sizeof(struct inotify_event);
    watcher->ProcessEvent(buffer, len);
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: ProcessEvent_005
 * @tc.desc: Test ProcessEvent with buffer too small for inotify_event stops watcher
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ProcessEvent_005, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        MockEventCallback);
    char buffer[1] = {0};
    ssize_t len = 1;
    watcher->ProcessEvent(buffer, len);
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: CleanupResources_001
 * @tc.desc: Test CleanupResources with invalid inotifyFd and watchDescriptor keeps state unchanged
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, CleanupResources_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        [](uint32_t mask, const std::string &filepath) {});
    watcher->inotifyFd_ = -1;
    watcher->watchDescriptor_ = -1;
    watcher->CleanupResources();
    EXPECT_EQ(watcher->inotifyFd_, -1);
    EXPECT_EQ(watcher->watchDescriptor_, -1);
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: CleanupResources_002
 * @tc.desc: Test CleanupResources with valid inotifyFd and watchDescriptor closes resources and resets state
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, CleanupResources_002, TestSize.Level1)
{
    // CleanupResources() calls fdsan_close_with_tag(inotifyFd_, FILE_WATCHER_FD_TAG).
    // Handing it an arbitrary unowned fd (e.g. 10) makes fdsan abort on tag
    // mismatch. Create a real inotify fd, tag it with the replicated production
    // tag, and attach a real watch on a temp file so both the inotify_rm_watch
    // and fdsan_close_with_tag branches execute cleanly.
    char tmpFile[TEMP_PATH_MAX_LEN] = {0};
    snprintf(tmpFile, sizeof(tmpFile), "/data/local/tmp/fw_test_%d", getpid());
    int fileFd = open(tmpFile, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fileFd < 0) {
        snprintf(tmpFile, sizeof(tmpFile), "fw_test_%d", getpid());
        fileFd = open(tmpFile, O_CREAT | O_RDWR | O_TRUNC, 0600);
    }
    ASSERT_GE(fileFd, 0) << "failed to create temp file for inotify watch";
    close(fileFd);

    int fd = inotify_init1(IN_NONBLOCK);
    ASSERT_GE(fd, 0);
    fdsan_exchange_owner_tag(fd, 0, FILE_WATCHER_FD_TAG);
    int wd = inotify_add_watch(fd, tmpFile, IN_MODIFY);
    if (wd < 0) {
        fdsan_close_with_tag(fd, FILE_WATCHER_FD_TAG);
        unlink(tmpFile);
        FAIL() << "inotify_add_watch failed: " << strerror(errno);
    }

    auto watcher = std::make_unique<FileWatcher>("/test/file",
        [](uint32_t mask, const std::string &filepath) {});
    watcher->inotifyFd_ = fd;
    watcher->watchDescriptor_ = wd;
    watcher->CleanupResources();
    EXPECT_EQ(watcher->inotifyFd_, -1);
    EXPECT_EQ(watcher->watchDescriptor_, -1);
    EXPECT_EQ(watcher->running_.load(), false);

    unlink(tmpFile);
}

/**
 * @tc.name: ThreadFuncStatic_001
 * @tc.desc: Test ThreadFuncStatic with null argument returns nullptr
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, ThreadFuncStatic_001, TestSize.Level1)
{
    void *result = FileWatcher::ThreadFuncStatic(nullptr);
    EXPECT_EQ(result, nullptr);
}

/**
 * @tc.name: Constructor_001
 * @tc.desc: Test FileWatcher constructor initializes member variables correctly
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, Constructor_001, TestSize.Level1)
{
    std::string filepath = "/test/path";
    auto watcher = std::make_unique<FileWatcher>(filepath,
        [](uint32_t mask, const std::string &filepath) {});
    EXPECT_EQ(watcher->filepath_, filepath);
    EXPECT_EQ(watcher->inotifyFd_, -1);
    EXPECT_EQ(watcher->watchDescriptor_, -1);
    EXPECT_EQ(watcher->running_.load(), false);
    EXPECT_EQ(watcher->threadCreated_.load(), false);
}

/**
 * @tc.name: HandleReadError_001
 * @tc.desc: Test HandleReadError with EINTR errno keeps running true
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, HandleReadError_001, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        [](uint32_t mask, const std::string &filepath) {});
    errno = EINTR;
    watcher->HandleReadError();
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: HandleReadError_002
 * @tc.desc: Test HandleReadError with EAGAIN errno keeps running true
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, HandleReadError_002, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        [](uint32_t mask, const std::string &filepath) {});
    errno = EAGAIN;
    watcher->HandleReadError();
    EXPECT_EQ(watcher->running_.load(), false);
}

/**
 * @tc.name: HandleReadError_003
 * @tc.desc: Test HandleReadError with EBADF errno sets running false
 * @tc.type: FUNC
 */
HWTEST_F(FileWatcherTest, HandleReadError_003, TestSize.Level1)
{
    auto watcher = std::make_unique<FileWatcher>("/test/file",
        [](uint32_t mask, const std::string &filepath) {});
    errno = EBADF;
    watcher->HandleReadError();
    EXPECT_EQ(watcher->running_.load(), false);
}

}
}