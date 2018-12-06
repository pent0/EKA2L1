/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <core/epoc/chunk.h>
#include <core/epoc/des.h>
#include <core/epoc/dll.h>
#include <core/epoc/hal.h>
#include <core/epoc/handle.h>
#include <core/epoc/panic.h>
#include <core/epoc/reqsts.h>
#include <core/epoc/svc.h>
#include <core/epoc/tl.h>
#include <core/epoc/uid.h>

#include <core/configure.h>

#ifdef ENABLE_SCRIPTING
#include <core/manager/script_manager.h>
#endif


#include <common/cvt.h>
#include <common/path.h>
#include <common/random.h>
#include <common/types.h>

#define CURL_STATICLIB
#ifdef WIN32
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "Normaliz.lib")
#endif

#include <date/tz.h>

#ifdef WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <ctime>

#include <common/e32inc.h>
#include <e32def.h>

namespace eka2l1::epoc {
    /* Do a final static assert check */
    static_assert(sizeof(TInt) == 4, "TInt Symbian size is not 4 bytes!");
    static_assert(sizeof(TUint) == 4, "TUint Symbian size is not 4 bytes!");
    static_assert(sizeof(TInt32) == 4, "TInt32 Symbian size is not 4 bytes!");
    static_assert(sizeof(TUint32) == 4, "TUint32 Symbian size is not 4 bytes!");
    static_assert(sizeof(TUint8) == 1, "TUint8 Symbian size is not 1 bytes!");
    static_assert(sizeof(TInt8) == 1, "TInt8 Symbian size is not 1 bytes!");
    static_assert(sizeof(TUint16) == 2, "TUint16 Symbian size is not 2 bytes!");
    static_assert(sizeof(TInt16) == 2, "TInt16 Symbian size is not 2 bytes!");
    static_assert(sizeof(TUint64) == 8, "TUint64 Symbian size is not 8 bytes!");
    static_assert(sizeof(TInt64) == 8, "TInt64 Symbian size is not 8 bytes!");

    /* TODO:                                       
     * 1. (bentokun) Implement global user data. Global user data should be allocated in global memory region.
    */

    /********************************/
    /*    GET/SET EXECUTIVE CALLS   */
    /*                              */
    /* Fast executive call, use for */
    /* get/set local data.          */
    /*                              */
    /********************************/

    /*! \brief Get the current heap allocator */
    BRIDGE_FUNC(eka2l1::ptr<void>, Heap) {
        auto local_data = current_local_data(sys);

        if (local_data.heap.ptr_address() == 0) {
            LOG_WARN("Allocator is not available.");
        }

        return local_data.heap;
    }

    BRIDGE_FUNC(eka2l1::ptr<void>, HeapSwitch, eka2l1::ptr<void> aNewHeap) {
        auto &local_data = current_local_data(sys);
        decltype(aNewHeap) old_heap = local_data.heap;
        local_data.heap = aNewHeap;

        return old_heap;
    }

    BRIDGE_FUNC(eka2l1::ptr<void>, TrapHandler) {
        auto local_data = current_local_data(sys);
        return local_data.trap_handler;
    }

    BRIDGE_FUNC(eka2l1::ptr<void>, SetTrapHandler, eka2l1::ptr<void> aNewHandler) {
        auto &local_data = current_local_data(sys);
        decltype(aNewHandler) old_handler = local_data.trap_handler;
        local_data.trap_handler = aNewHandler;

        return local_data.trap_handler;
    }

    BRIDGE_FUNC(eka2l1::ptr<void>, ActiveScheduler) {
        auto local_data = current_local_data(sys);
        return local_data.scheduler;
    }

    BRIDGE_FUNC(void, SetActiveScheduler, eka2l1::ptr<void> aNewScheduler) {
        auto &local_data = current_local_data(sys);
        local_data.scheduler = aNewScheduler;
    }

    BRIDGE_FUNC(void, After, TInt aMicroSecs, eka2l1::ptr<epoc::request_status> aStatus) {
        sys->get_kernel_system()->crr_thread()->after(aStatus.get(sys->get_memory_system()),
            aMicroSecs);
    }

    /****************************/
    /* PROCESS */
    /***************************/

    BRIDGE_FUNC(TInt, ProcessExitType, TInt aHandle) {
        eka2l1::memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr_real;

        // 0xffff8000 is a kernel mapping for current process
        if (aHandle != 0xffff8000) {
            // Unlike Symbian, process is not a kernel object here
            // Its handle contains the process's uid
            pr_real = kern->get_process(aHandle);
        } else {
            pr_real = kern->crr_process();
        }

        if (!pr_real) {
            LOG_ERROR("ProcessExitType: Invalid process");
            return 0;
        }

        return static_cast<TInt>(pr_real->get_exit_type());
    }

    BRIDGE_FUNC(void, ProcessRendezvous, TInt aRendezvousCode) {
        sys->get_kernel_system()->crr_process()->rendezvous(aRendezvousCode);
    }

    BRIDGE_FUNC(void, ProcessFilename, TInt aProcessHandle, eka2l1::ptr<TDes8> aDes) {
        eka2l1::memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr_real;

        // 0xffff8000 is a kernel mapping for current process
        if (aProcessHandle != 0xffff8000) {
            // Unlike Symbian, process is not a kernel object here
            // Its handle contains the process's uid
            pr_real = kern->get_process(aProcessHandle);
        } else {
            pr_real = kern->crr_process();
        }

        if (!pr_real) {
            LOG_ERROR("SvcProcessFileName: Invalid process");
            return;
        }

        TDes8 *des = aDes.get(mem);
        des->Assign(sys, pr_real->name());
    }

    BRIDGE_FUNC(TInt, ProcessGetId, TInt aHandle) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr_real;

        // 0xffff8000 is a kernel mapping for current process
        if (aHandle != 0xffff8000) {
            // Unlike Symbian, process is not a kernel object here
            // Its handle contains the process's uid
            pr_real = kern->get_process(aHandle);
        } else {
            pr_real = kern->crr_process();
        }

        if (!pr_real) {
            LOG_ERROR("ProcessGetId: Invalid process");
            return KErrBadHandle;
        }

        return pr_real->unique_id();
    }

    BRIDGE_FUNC(void, ProcessType, address pr, eka2l1::ptr<TUidType> uid_type) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr_real;

        // 0xffff8000 is a kernel mapping for current process
        if (pr != 0xffff8000) {
            // Unlike Symbian, process is not a kernel object here
            // Its handle contains the process's uid
            pr_real = kern->get_process(pr);
        } else {
            pr_real = kern->crr_process();
        }

        if (!pr_real) {
            LOG_ERROR("SvcProcessType: Invalid process");
            return;
        }

        TUidType *type = uid_type.get(mem);
        auto tup = pr_real->get_uid_type();

        type->uid1 = std::get<0>(tup);
        type->uid2 = std::get<1>(tup);
        type->uid3 = std::get<2>(tup);
    }

    BRIDGE_FUNC(TInt, ProcessDataParameterLength, TInt aSlot) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr crr_process = kern->crr_process();

        if (aSlot >= 16 || aSlot < 0) {
            LOG_ERROR("Invalid slot (slot: {} >= 16 or < 0)", aSlot);
            return KErrArgument;
        }

        auto slot = crr_process->get_arg_slot(aSlot);

        if (!slot || !slot->used) {
            LOG_ERROR("Getting descriptor length of unused slot: {}", aSlot);
            return KErrNotFound;
        }

        return static_cast<TInt>(slot->data.size());
    }

    BRIDGE_FUNC(TInt, ProcessGetDataParameter, TInt aSlot, eka2l1::ptr<TUint8> aData, TInt aLength) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->crr_process();

        if (aSlot >= 16 || aSlot < 0) {
            LOG_ERROR("Invalid slot (slot: {} >= 16 or < 0)", aSlot);
            return KErrArgument;
        }

        auto slot = *pr->get_arg_slot(aSlot);

        if (!slot.used) {
            LOG_ERROR("Parameter slot unused, error: {}", aSlot);
            return KErrNotFound;
        }

        if (aLength < slot.data.size()) {
            return KErrArgument;
        }

        TUint8 *data = aData.get(sys->get_memory_system());
        std::copy(slot.data.begin(), slot.data.end(), data);

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, ProcessSetDataParameter, TInt aHandle, TInt aSlot, eka2l1::ptr<TUint8> aData, TInt aDataSize) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->get_process(aHandle);

        if (!pr) {
            return KErrBadHandle;
        }

        if (aSlot < 0 || aSlot >= 16) {
            LOG_ERROR("Invalid parameter slot: {}, slot number must be in range of 0-15", aSlot);
            return KErrArgument;
        }

        auto slot = *pr->get_arg_slot(aSlot);

        if (slot.used) {
            LOG_ERROR("Can't set parameter of an used slot: {}", aSlot);
            return KErrInUse;
        }

        pr->set_arg_slot(aSlot, aData.get(sys->get_memory_system()), aDataSize);
        return KErrNone;
    }

    BRIDGE_FUNC(TInt, ProcessCommandLineLength, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->get_process(aHandle);

        if (!pr) {
            return KErrBadHandle;
        }

        return static_cast<TInt>(pr->get_cmd_args().length());
    }

    BRIDGE_FUNC(void, ProcessCommandLine, TInt aHandle, eka2l1::ptr<TDes8> aData) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->get_process(aHandle);

        if (!pr) {
            LOG_WARN("Process not found with handle: 0x{:x}", aHandle);
            return;
        }

        TDes8 *data = aData.get(sys->get_memory_system());

        if (!data) {
            return;
        }

        std::u16string arg = pr->get_cmd_args();

        // x2 the size of data since this is an u16string
        if (data->iMaxLength < arg.length() * 2) {
            LOG_WARN("Not enough data to store command line, abort");
            return;
        }
        
        TUint8 *data_ptr = data->Ptr(sys);

        memcpy(data_ptr, arg.data(), arg.length() * 2);
        data->SetLength(sys, static_cast<TUint32>(arg.length() * 2));
    }

    BRIDGE_FUNC(void, ProcessSetFlags, TInt aHandle, TUint aClearMask, TUint aSetMask) {
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr = kern->get_process(aHandle);

        uint32_t org_flags = pr->get_flags();
        uint32_t new_flags = ((org_flags & ~aClearMask) | aSetMask);
        new_flags = (new_flags ^ org_flags);

        pr->set_flags(org_flags ^ new_flags);
    }

    BRIDGE_FUNC(TInt, ProcessSetPriority, TInt aHandle, TInt aProcessPriority) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->get_process(aHandle);

        if (!pr) {
            return KErrBadHandle;
        }

        pr->set_priority(static_cast<eka2l1::kernel::process_priority>(aProcessPriority));

        return KErrNone;
    }

    BRIDGE_FUNC(void, ProcessResume, TInt aHandle) {
        process_ptr pr = sys->get_kernel_system()->get_process(aHandle);

        if (!pr) {
            return;
        }

        pr->run();
    }

    BRIDGE_FUNC(void, ProcessLogon, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestSts, TBool aRendezvous) {
        LOG_TRACE("Logon requested from thread {}", sys->get_kernel_system()->crr_thread()->name());

        process_ptr pr = sys->get_kernel_system()->get_process(aHandle);

        if (!pr) {
            return;
        }

        pr->logon(aRequestSts.get(sys->get_kernel_system()->get_memory_system()), aRendezvous);
    }

    BRIDGE_FUNC(TInt, ProcessLogonCancel, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestSts, TBool aRendezvous) {
        process_ptr pr = sys->get_kernel_system()->get_process(aHandle);

        if (!pr) {
            return KErrBadHandle;
        }

        bool logon_success = pr->logon_cancel(aRequestSts.get(sys->get_kernel_system()->get_memory_system()), aRendezvous);

        if (logon_success) {
            return KErrNone;
        }

        return KErrGeneral;
    }

    /********************/
    /* TLS */
    /*******************/

    BRIDGE_FUNC(eka2l1::ptr<void>, DllTls, TInt aHandle, TInt aDllUid) {
        eka2l1::kernel::thread_local_data dat = current_local_data(sys);
        thread_ptr thr = sys->get_kernel_system()->crr_thread();

        for (const auto &tls : dat.tls_slots) {
            if (tls.handle == aHandle) {
                return tls.pointer;
            }
        }

        LOG_WARN("TLS for 0x{:x}, thread {} return 0, may results unexpected crash", static_cast<TUint>(aHandle),
            thr->name());

        return eka2l1::ptr<void>(0);
    }

    BRIDGE_FUNC(TInt, DllSetTls, TInt aHandle, TInt aDllUid, eka2l1::ptr<void> aPtr) {
        eka2l1::kernel::tls_slot *slot = get_tls_slot(sys, aHandle);

        if (!slot) {
            return KErrNoMemory;
        }

        slot->pointer = aPtr;

        thread_ptr thr = sys->get_kernel_system()->crr_thread();

        LOG_TRACE("TLS set for 0x{:x}, ptr: 0x{:x}, thread {}", static_cast<TUint>(aHandle), aPtr.ptr_address(),
            thr->name());

        return KErrNone;
    }

    BRIDGE_FUNC(void, DllFreeTLS, TInt iHandle) {
        thread_ptr thr = sys->get_kernel_system()->crr_thread();
        thr->close_tls_slot(*thr->get_tls_slot(iHandle, iHandle));

        LOG_TRACE("TLS slot closed for 0x{:x}, thread {}", static_cast<TUint>(iHandle), thr->name());
    }

    BRIDGE_FUNC(void, DllFileName, TInt aEntryAddress, eka2l1::ptr<TDes8> aFullPathPtr) {
        std::optional<std::u16string> dll_full_path = get_dll_full_path(sys, aEntryAddress);

        if (!dll_full_path) {
            LOG_WARN("Unable to find DLL name for address: 0x{:x}", aEntryAddress);
            return;
        }

        std::string path_utf8 = common::ucs2_to_utf8(*dll_full_path);
        LOG_TRACE("Find DLL for address 0x{:x} with name: {}", static_cast<TUint>(aEntryAddress),
            path_utf8);

        aFullPathPtr.get(sys->get_memory_system())->Assign(sys, path_utf8);
    }

    /***********************************/
    /* LOCALE */
    /**********************************/

    /*
    * Warning: It's not possible to set the UTC time and offset in the emulator at the moment.
    */

    /*! \brief Get the UTC offset in seconds. 
     *
     * This was proved to be in seconds by the use of it in us_time.cpp (TTime::HomeTimeSecure), where 
     * the offset was passed as a constructor argument to TTimeIntervalSeconds.
     *
     * \returns The UTC offset, in seconds.
     */
    BRIDGE_FUNC(TInt, UTCOffset) {
        return date::current_zone()->get_info(std::chrono::system_clock::now()).offset.count();
    }

    enum : uint64_t {
        microsecs_per_sec = 1000000,
        ad_epoc_dist_microsecs = 62167132800 * microsecs_per_sec
    };

    BRIDGE_FUNC(TInt, TimeNow, eka2l1::ptr<TUint64> aTime, eka2l1::ptr<TInt> aUTCOffset) {
        TUint64 *time = aTime.get(sys->get_memory_system());
        TInt *offset = aUTCOffset.get(sys->get_memory_system());

        LOG_INFO("EKA2L1 may download timezone database to your Downloads folder if the database doesn't exist");
        LOG_INFO("Disable this info using core option: disableadd");

        // The time is since EPOC, we need to convert it to first of AD
        *time = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
            + ad_epoc_dist_microsecs;
        *offset = date::current_zone()->get_info(std::chrono::system_clock::now()).offset.count();

        return KErrNone;
    }

    /********************************************/
    /* IPC */
    /*******************************************/

    BRIDGE_FUNC(void, SetSessionPtr, TInt aMsgHandle, TUint aSessionAddress) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        ipc_msg_ptr msg = kern->get_msg(aMsgHandle);

        if (!msg) {
            return;
        }

        msg->msg_session->set_cookie_address(aSessionAddress);
    }

    BRIDGE_FUNC(TInt, MessageComplete, TInt aMsgHandle, TInt aVal) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        ipc_msg_ptr msg = kern->get_msg(aMsgHandle);

        if (!msg) {
            return KErrBadHandle;
        }

        if (msg->msg_session->get_server()->name() == "!AppListServer" &&
            msg->function == 65)
            {
                LOG_TRACE("AppList rule-based");
            }

        if (msg->request_sts) {
            *msg->request_sts = aVal;
            msg->own_thr->signal_request();
        }
        
        // LOG_TRACE("Message completed with code: {}, thread to signal: {}", aVal, msg->own_thr->name());

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, MessageKill, TInt aHandle, TExitType aExitType, TInt aReason, eka2l1::ptr<TDesC8> aCage) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        std::string exit_cage = aCage.get(mem)->StdString(sys);
        std::optional<std::string> exit_description;

        ipc_msg_ptr msg = kern->get_msg(aHandle);

        if (!msg || !msg->own_thr) {
            return KErrBadHandle;
        }

        std::string thread_name = msg->own_thr->name();

        if (is_panic_category_action_default(exit_cage)) {
            exit_description = get_panic_description(exit_cage, aReason);

            switch (aExitType) {
            case TExitType::panic:
                LOG_TRACE("Thread {} paniced by message with cagetory: {} and exit code: {} {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            case TExitType::kill:
                LOG_TRACE("Thread {} forcefully killed by message with cagetory: {} and exit code: {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            case TExitType::terminate:
            case TExitType::pending:
                LOG_TRACE("Thread {} terminated peacefully by message with cagetory: {} and exit code: {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            default:
                return KErrArgument;
            }
        }

#ifdef ENABLE_SCRIPTING
        sys->get_manager_system()->get_script_manager()->call_panics(exit_cage, aReason);
#endif

        kern->get_thread_scheduler()->stop(msg->own_thr);
        kern->prepare_reschedule();

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, MessageGetDesLength, TInt aHandle, TInt aParam) {
        if (aParam < 0) {
            return KErrArgument;
        }

        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        ipc_msg_ptr msg = kern->get_msg(aHandle);

        if (!msg) {
            return KErrBadHandle;
        }

        service::ipc_context context;
        context.msg = msg;
        context.sys = sys;

        const auto try_des16 = context.get_arg<std::u16string>(aParam);

        // Reverse the order for safety
        if (!try_des16) {
            const auto try_des8 = context.get_arg<std::string>(aParam);

            if (!try_des8) {
                return KErrBadDescriptor;
            }

            return static_cast<TInt>(try_des8->length());
        }

        return static_cast<TInt>(try_des16->length());
    }

    BRIDGE_FUNC(TInt, MessageGetDesMaxLength, TInt aHandle, TInt aParam) {
        if (aParam < 0) {
            return KErrArgument;
        }

        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        ipc_msg_ptr msg = kern->get_msg(aHandle);

        if (!msg) {
            return KErrBadHandle;
        }

        service::ipc_context context;
        context.msg = msg;
        context.sys = sys;

        const ipc_arg_type type = context.msg->args.get_arg_type(aParam);

        if ((int)type & (int)ipc_arg_type::flag_des) {
            return ExtractDesMaxLength(reinterpret_cast<TDes8 *>(
                mem->get_real_pointer(msg->args.args[aParam])));
        }

        return KErrGeneral;
    }

    struct TIpcCopyInfo {
        eka2l1::ptr<TUint8> iTargetPtr;
        int iTargetLength;
        int iFlags;
    };

    static_assert(sizeof(TIpcCopyInfo) == 12, "Size of IPCCopy struct is 12");

    const TInt KChunkShiftBy0 = 0;
    const TInt KChunkShiftBy1 = (TInt)0x80000000;
    const TInt KIpcDirRead = 0;
    const TInt KIpcDirWrite = 0x10000000;

    BRIDGE_FUNC(TInt, MessageIpcCopy, TInt aHandle, TInt aParam, eka2l1::ptr<TIpcCopyInfo> aInfo, TInt aStartOffset) {
        if (!aInfo || aParam < 0) {
            return KErrArgument;
        }

        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        TIpcCopyInfo *info = aInfo.get(mem);
        ipc_msg_ptr msg = kern->get_msg(aHandle);

        if (!msg) {
            return KErrBadHandle;
        }

        bool des8 = true;
        if (info->iFlags & KChunkShiftBy1) {
            des8 = false;
        }

        bool read = true;
        if (info->iFlags & KIpcDirWrite) {
            read = false;
        }

        if (read) {
            service::ipc_context context;
            context.sys = sys;
            context.msg = msg;

            if (des8) {
                const auto arg_request = context.get_arg<std::string>(aParam);

                if (!arg_request) {
                    return KErrBadDescriptor;
                }

                // Target length is the descriptor length, either 8-bit or 16-bit, not total bytes that can be stored.
                if (arg_request->length() - aStartOffset > info->iTargetLength) {
                    return KErrNoMemory;
                }

                memcpy(info->iTargetPtr.get(mem), arg_request->data() + aStartOffset, arg_request->size() - aStartOffset);

                return KErrNone;
            }

            const auto arg_request = context.get_arg<std::u16string>(aParam);

            if (!arg_request) {
                return KErrBadDescriptor;
            }

            if (arg_request->length() - aStartOffset > info->iTargetLength) {
                return KErrNoMemory;
            }

            memcpy(info->iTargetPtr.get(mem), reinterpret_cast<const TUint8 *>(arg_request->data()) + aStartOffset * 2,
                (arg_request->size() - aStartOffset) * 2);

            return KErrNone;
        }

        service::ipc_context context;
        context.sys = sys;
        context.msg = msg;

        std::string content;
        content.resize(des8 ? (aStartOffset + info->iTargetLength) : (aStartOffset + info->iTargetLength) * 2);

        memcpy(&content[des8 ? aStartOffset : aStartOffset * 2], info->iTargetPtr.get(mem), des8 ? info->iTargetLength : info->iTargetLength * 2);
        bool result = context.write_arg_pkg(aParam, reinterpret_cast<uint8_t *>(&content[0]), content.length());

        if (!result) {
            return KErrBadDescriptor;
        }

        return KErrNone;
    }

    void query_security_info(eka2l1::process_ptr process, epoc::TSecurityInfo *info) {
        assert(process);

        kernel::security_info sec_info = process->get_sec_info();
        memcpy(info, &sec_info, sizeof(sec_info));
    }

    BRIDGE_FUNC(void, ProcessSecurityInfo, TInt aProcessHandle, eka2l1::ptr<epoc::TSecurityInfo> aSecInfo) {
        epoc::TSecurityInfo *sec_info = aSecInfo.get(sys->get_memory_system());
        kernel_system *kern = sys->get_kernel_system();

        process_ptr pr = std::dynamic_pointer_cast<kernel::process>(kern->get_kernel_obj(aProcessHandle));
        query_security_info(pr, sec_info);
    }

    BRIDGE_FUNC(void, ThreadSecurityInfo, TInt aThreadHandle, eka2l1::ptr<epoc::TSecurityInfo> aSecInfo) {
        epoc::TSecurityInfo *sec_info = aSecInfo.get(sys->get_memory_system());
        kernel_system *kern = sys->get_kernel_system();

        thread_ptr thr = std::dynamic_pointer_cast<kernel::thread>(kern->get_kernel_obj(aThreadHandle));

        if (!thr) {
            LOG_ERROR("Thread handle invalid 0x{:x}", aThreadHandle);
            return;
        }

        query_security_info(thr->owning_process(), sec_info);
    }

    BRIDGE_FUNC(void, MessageSecurityInfo, TInt aMessageHandle, eka2l1::ptr<epoc::TSecurityInfo> aSecInfo) {
        epoc::TSecurityInfo *sec_info = aSecInfo.get(sys->get_memory_system());
        kernel_system *kern = sys->get_kernel_system();

        eka2l1::ipc_msg_ptr msg = kern->get_msg(aMessageHandle);

        if (!msg) {
            LOG_ERROR("Thread handle invalid 0x{:x}", aMessageHandle);
            return;
        }

        query_security_info(msg->own_thr->owning_process(), sec_info);
    }

    BRIDGE_FUNC(TInt, ServerCreate, eka2l1::ptr<TDesC8> aServerName, TInt aMode) {
        kernel_system *kern = sys->get_kernel_system();
        std::string server_name = aServerName.get(sys->get_memory_system())->StdString(sys);

        uint32_t handle = kern->create_server(server_name);

        if (handle != INVALID_HANDLE) {
            LOG_TRACE("Server {} created", server_name);
        }

        return handle;
    }

    BRIDGE_FUNC(void, ServerReceive, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestStatus, eka2l1::ptr<TAny> aDataPtr) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        server_ptr server = kern->get_server(aHandle);

        if (!server) {
            return;
        }

        LOG_TRACE("Receive requested from {}", server->name());

        server->receive_async_lle(aRequestStatus.get(mem),
            reinterpret_cast<service::message2 *>(aDataPtr.get(mem)));
    }

    BRIDGE_FUNC(void, ServerCancel, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        server_ptr server = kern->get_server(aHandle);

        if (!server) {
            return;
        }

        server->cancel_async_lle();
    }

    BRIDGE_FUNC(TInt, SessionCreate, eka2l1::ptr<TDesC8> aServerName, TInt aMsgSlot, eka2l1::ptr<void> aSec, TInt aMode) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        std::string server_name = aServerName.get(mem)->StdString(sys);
        server_ptr server = kern->get_server_by_name(server_name);

        if (!server) {
            LOG_TRACE("Create session to unexist server: {}", server_name);
            return KErrNotFound;
        }

        uint32_t handle = kern->create_session(server, aMsgSlot);

        if (handle == INVALID_HANDLE) {
            return KErrGeneral;
        }

        LOG_TRACE("New session connected to {} with id {}", server_name, handle);

        return handle;
    }

    BRIDGE_FUNC(TInt, SessionShare, eka2l1::ptr<TInt> aHandle, TInt aShare) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        TInt *handle = aHandle.get(mem);

        session_ptr ss = kern->get_session(*handle);

        if (!ss) {
            return KErrBadHandle;
        }

        if (aShare == 2) {
            // Explicit attach: other process uses IPC can open this handle, so do threads! :D
            ss->set_access_type(kernel::access_type::global_access);
        } else {
            ss->set_access_type(kernel::access_type::local_access);
        }

        int old_handle = *handle;

        // Weird behavior, suddenly it wants to mirror handle then close the old one
        // Clean its identity :D
        *handle = kern->mirror(kern->crr_thread(), *handle, kernel::owner_type::process);
        kern->close(old_handle);

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, SessionSendSync, TInt aHandle, TInt aOrd, eka2l1::ptr<TAny> aIpcArgs,
        eka2l1::ptr<epoc::request_status> aStatus) {
        //LOG_TRACE("Send using handle: {}", (aHandle & 0x8000) ? (aHandle & ~0x8000) : (aHandle));

        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        // Dispatch the header
        ipc_arg arg;
        TInt *arg_header = aIpcArgs.cast<TInt>().get(mem);

        if (aIpcArgs) {
            for (uint8_t i = 0; i < 4; i++) {
                arg.args[i] = *arg_header++;
            }

            arg.flag = *arg_header & (((1 << 12) - 1) | (int)ipc_arg_pin::pin_mask);
        }

        session_ptr ss = kern->get_session(aHandle);

        if (!ss) {
            return KErrBadHandle;
        }

        if (!aStatus) {
            LOG_TRACE("Sending a blind sync message");
        }
        
        return ss->send_receive_sync(aOrd, arg, aStatus.get(mem));
    }

    BRIDGE_FUNC(TInt, SessionSend, TInt aHandle, TInt aOrd, eka2l1::ptr<TAny> aIpcArgs,
        eka2l1::ptr<epoc::request_status> aStatus) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        // Dispatch the header
        ipc_arg arg;
        TInt *arg_header = aIpcArgs.cast<TInt>().get(mem);

        if (aIpcArgs) {
            for (uint8_t i = 0; i < 4; i++) {
                arg.args[i] = *arg_header++;
            }

            arg.flag = *arg_header & (((1 << 12) - 1) | (int)ipc_arg_pin::pin_mask);
        }

        session_ptr ss = kern->get_session(aHandle);

        if (!ss) {
            return KErrBadHandle;
        }

        if (!aStatus) {
            LOG_TRACE("Sending a blind async message");
        }

        return ss->send_receive(aOrd, arg, aStatus.get(mem));
    }

    /**********************************/
    /* TRAP/LEAVE */
    /*********************************/

    BRIDGE_FUNC(eka2l1::ptr<void>, LeaveStart) {
        LOG_CRITICAL("Leave started!");

        eka2l1::thread_ptr thr = sys->get_kernel_system()->crr_thread();
        thr->increase_leave_depth();

        return current_local_data(sys).trap_handler;
    }

    BRIDGE_FUNC(void, LeaveEnd) {
        eka2l1::thread_ptr thr = sys->get_kernel_system()->crr_thread();
        thr->decrease_leave_depth();

        if (thr->is_invalid_leave()) {
            LOG_CRITICAL("Invalid leave, leave depth is negative!");
        }

        LOG_TRACE("Leave trapped by trap handler.");
    }

    BRIDGE_FUNC(TInt, DebugMask) {
        return 0;
    }

    BRIDGE_FUNC(TInt, DebugMaskIndex, TInt aIdx) {
        return 0;
    }

    BRIDGE_FUNC(TInt, HalFunction, TInt aCagetory, TInt aFunc, eka2l1::ptr<TInt> a1, eka2l1::ptr<TInt> a2) {
        memory_system *mem = sys->get_memory_system();

        int *arg1 = a1.get(mem);
        int *arg2 = a2.get(mem);

        return do_hal(sys, aCagetory, aFunc, arg1, arg2);
    }

    /**********************************/
    /* CHUNK */
    /*********************************/

    BRIDGE_FUNC(TInt, ChunkCreate, TOwnerType aOwnerType, eka2l1::ptr<TDesC8> aName, eka2l1::ptr<TChunkCreate> aChunkCreate) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TChunkCreate createInfo = *aChunkCreate.get(mem);
        TDesC8 *name = aName.get(mem);

        auto lol
            = name->StdString(sys);

        kernel::chunk_type type;
        kernel::chunk_access access = kernel::chunk_access::local;
        kernel::chunk_attrib att = decltype(att)::none;

        // Fetch chunk type
        if (createInfo.iAtt & TChunkCreate::EDisconnected) {
            type = kernel::chunk_type::disconnected;
        } else if (createInfo.iAtt & TChunkCreate::EDoubleEnded) {
            type = kernel::chunk_type::double_ended;
        } else {
            type = kernel::chunk_type::normal;
        }

        // Fetch chunk access
        if (!(createInfo.iAtt & TChunkCreate::EGlobal)) {
            access = kernel::chunk_access::local;
        } else {
            access = kernel::chunk_access::global;
        }

        if (access == decltype(access)::global && name->Length() == 0) {
            att = kernel::chunk_attrib::anonymous;
        }

        uint32_t handle = kern->create_chunk(name->StdString(sys), createInfo.iInitialBottom, createInfo.iInitialTop,
            createInfo.iMaxSize, prot::read_write, type, access, att,
            aOwnerType == EOwnerProcess ? kernel::owner_type::process : kernel::owner_type::thread);

        if (handle == INVALID_HANDLE) {
            return KErrNoMemory;
        }

        return handle;
    }

    BRIDGE_FUNC(TInt, ChunkMaxSize, TInt aChunkHandle) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return KErrBadHandle;
        }

        chunk_ptr chunk = std::dynamic_pointer_cast<kernel::chunk>(obj);
        return static_cast<TInt>(chunk->get_max_size());
    }

    BRIDGE_FUNC(eka2l1::ptr<TUint8>, ChunkBase, TInt aChunkHandle) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return 0;
        }

        chunk_ptr chunk = std::dynamic_pointer_cast<kernel::chunk>(obj);
        return chunk->base();
    }

    BRIDGE_FUNC(TInt, ChunkAdjust, TInt aChunkHandle, TInt aType, TInt a1, TInt a2) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return KErrBadHandle;
        }

        chunk_ptr chunk = std::dynamic_pointer_cast<kernel::chunk>(obj);

        auto fetch = [aType](chunk_ptr chunk, int a1, int a2) -> bool {
            switch (aType) {
            case 0:
                return chunk->adjust(a1);

            case 1:
                return chunk->adjust_de(a1, a2);

            case 2:
                return chunk->commit(a1, a2);

            case 3:
                return chunk->decommit(a1, a2);

            case 4: // Allocate. Afaik this adds more commit size
                return chunk->allocate(a1);

            case 5:
            case 6:
                return true;
            }

            return false;
        };

        bool res = fetch(chunk, a1, a2);

        if (!res) {
            return KErrGeneral;
        }

        return KErrNone;
    }

    /********************/
    /* SYNC PRIMITIVES  */
    /********************/

    BRIDGE_FUNC(TInt, SemaphoreCreate, eka2l1::ptr<TDesC8> aSemaName, TInt aInitCount, TOwnerType aOwnerType) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TDesC8 *desname = aSemaName.get(mem);
        kernel::owner_type owner = (aOwnerType == EOwnerProcess) ? kernel::owner_type::process : kernel::owner_type::thread;

        uint32_t sema = kern->create_sema(!desname ? "" : desname->StdString(sys).c_str(),
            aInitCount, owner, !desname ? kernel::access_type::local_access : kernel::access_type::global_access);

        if (sema == INVALID_HANDLE) {
            return KErrGeneral;
        }

        return sema;
    }

    BRIDGE_FUNC(TInt, SemaphoreWait, TInt aSemaHandle, TInt aTimeout) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        sema_ptr sema = std::dynamic_pointer_cast<kernel::semaphore>(kern->get_kernel_obj(aSemaHandle));

        if (!sema) {
            return KErrBadHandle;
        }

        if (aTimeout) {
            LOG_WARN("Semaphore timeout unimplemented");
        }

        sema->wait();
        return KErrNone;
    }

    BRIDGE_FUNC(void, SemaphoreSignal, TInt aSemaHandle) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        sema_ptr sema = std::dynamic_pointer_cast<kernel::semaphore>(kern->get_kernel_obj(aSemaHandle));

        if (!sema) {
            return;
        }

        sema->signal(1);
    }

    BRIDGE_FUNC(void, SemaphoreSignalN, TInt aSemaHandle, TInt aSigCount) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        sema_ptr sema = std::dynamic_pointer_cast<kernel::semaphore>(kern->get_kernel_obj(aSemaHandle));

        if (!sema) {
            return;
        }

        sema->signal(aSigCount);
    }

    BRIDGE_FUNC(TInt, MutexCreate, eka2l1::ptr<TDesC8> aMutexName, TOwnerType aOwnerType) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TDesC8 *desname = aMutexName.get(mem);
        kernel::owner_type owner = (aOwnerType == EOwnerProcess) ? kernel::owner_type::process : kernel::owner_type::thread;

        uint32_t mut = kern->create_mutex(!desname ? "" : desname->StdString(sys), false,
            owner, !desname ? kernel::access_type::local_access : kernel::access_type::global_access);

        if (mut == INVALID_HANDLE) {
            return KErrGeneral;
        }

        return mut;
    }

    BRIDGE_FUNC(void, WaitForAnyRequest) {
        sys->get_kernel_system()->crr_thread()->wait_for_any_request();
    }

    BRIDGE_FUNC(void, RequestSignal, int aSignalCount) {
        sys->get_kernel_system()->crr_thread()->signal_request(aSignalCount);
    }

    /***********************************************/
    /* HANDLE FUNCTIONS   */
    /*                    */
    /* Thread independent */
    /**********************************************/

    BRIDGE_FUNC(TInt, ObjectNext, TObjectType aObjectType, eka2l1::ptr<TDes8> aName, eka2l1::ptr<TFindHandle> aHandleFind) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TFindHandle *handle = aHandleFind.get(mem);
        std::string name = aName.get(mem)->StdString(sys);

        LOG_TRACE("Finding object name: {}", name);

        std::optional<find_handle> info = kern->find_object(name, handle->iHandle, static_cast<kernel::object_type>(aObjectType));

        if (!info) {
            return KErrNotFound;
        }

        handle->iHandle = info->index;
        handle->iObjIdLow = static_cast<uint32_t>(info->object_id);
        handle->iObjIdHigh = info->object_id >> 32;

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, HandleClose, TInt aHandle) {
        if (aHandle & 0x8000) {
            return KErrGeneral;
        }

        int res = sys->get_kernel_system()->close(aHandle);

        return res;
    }

    BRIDGE_FUNC(TInt, HandleDuplicate, TInt aThreadHandle, TOwnerType aOwnerType, TInt aDupHandle) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        uint32_t res = kern->mirror(kern->get_thread_by_handle(aThreadHandle), aDupHandle,
            (aOwnerType == EOwnerProcess) ? kernel::owner_type::process : kernel::owner_type::thread);

        return res;
    }

    BRIDGE_FUNC(TInt, HandleOpenObject, TObjectType aObjectType, eka2l1::ptr<epoc::TDesC8> aName, TInt aOwnerType) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        std::string obj_name = aName.get(mem)->StdString(sys);

        auto obj_info = kern->find_object(obj_name, 0, static_cast<eka2l1::kernel::object_type>(aObjectType));

        if (!obj_info) {
            return KErrNotFound;
        }

        uint64_t id = obj_info->object_id;
        kernel_obj_ptr obj = kern->get_kernel_obj_by_id(id);

        uint32_t ret_handle = kern->mirror(obj, static_cast<eka2l1::kernel::owner_type>(aOwnerType));

        if (ret_handle != 0xFFFFFFFF) {
            return ret_handle;
        }

        return KErrGeneral;
    }

    BRIDGE_FUNC(void, HandleName, TInt aHandle, eka2l1::ptr<TDes8> aName) {
        kernel_system *kern = sys->get_kernel_system();
        kernel_obj_ptr obj = kern->get_kernel_obj(aHandle);

        if (!obj) {
            if (aHandle == 0xFFFF8001) {
                obj = kern->crr_thread();
            } else {
                return;
            }
        }

        TDes8 *desname = aName.get(sys->get_memory_system());
        desname->Assign(sys, obj->name());
    }

    /******************************/
    /* CODE SEGMENT */
    /*****************************/

    BRIDGE_FUNC(TInt, StaticCallList, eka2l1::ptr<TInt> aTotal, eka2l1::ptr<TUint32> aList) {
        memory_system *mem = sys->get_memory_system();

        TUint32 *list_ptr = aList.get(mem);
        TInt *total = aTotal.get(mem);

        std::vector<uint32_t> list = epoc::query_entries(sys);

        *total = static_cast<TInt>(list.size());
        memcpy(list_ptr, list.data(), sizeof(TUint32) * *total);

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, WaitDllLock) {
        sys->get_kernel_system()->crr_process()->wait_dll_lock();

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, ReleaseDllLock) {
        sys->get_kernel_system()->crr_process()->signal_dll_lock();

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, LibraryAttach, TInt aHandle, eka2l1::ptr<TInt> aNumEps, eka2l1::ptr<TUint32> aEpList) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        library_ptr lib = std::dynamic_pointer_cast<kernel::library>(kern->get_kernel_obj(aHandle));

        if (!lib) {
            return KErrBadHandle;
        }

        std::vector<uint32_t> entries = lib->attach();

        *aNumEps.get(mem) = entries.size();

        for (size_t i = 0; i < entries.size(); i++) {
            (aEpList.get(mem))[i] = entries[i];
        }

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, LibraryLookup, TInt aHandle, TInt aOrdinalIndex) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        library_ptr lib = std::dynamic_pointer_cast<kernel::library>(kern->get_kernel_obj(aHandle));

        if (!lib) {
            return 0;
        }

        std::optional<uint32_t> func_addr = lib->get_ordinal_address(static_cast<uint8_t>(aOrdinalIndex));

        if (!func_addr) {
            return 0;
        }

        return *func_addr;
    }

    BRIDGE_FUNC(TInt, LibraryAttached, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        library_ptr lib = std::dynamic_pointer_cast<kernel::library>(kern->get_kernel_obj(aHandle));

        if (!lib) {
            return KErrBadHandle;
        }

        bool attached_result = lib->attached();

        if (!attached_result) {
            return KErrGeneral;
        }

        return KErrNone;
    }

    /************************/
    /* USER SERVER */
    /***********************/

    BRIDGE_FUNC(TInt, UserSvrRomHeaderAddress) {
        // EKA1
        if ((int)sys->get_kernel_system()->get_epoc_version() <= (int)epocver::epoc6) {
            return 0x50000000;
        }

        // EKA2
        return 0x80000000;
    }

    /************************/
    /*  THREAD  */
    /************************/

    struct thread_create_info_expand {
        int handle;
        int type;
        address func_ptr;
        address ptr;
        address supervisor_stack;
        int supervisor_stack_size;
        address user_stack;
        int user_stack_size;
        kernel::thread_priority init_thread_priority;
        TPtrC8 name;
        int total_size;

        address allocator;
        int heap_initial_size;
        int heap_max_size;
        int flags;
    };

    static_assert(sizeof(thread_create_info_expand) == 64,
        "Thread create info struct size invalid");

    BRIDGE_FUNC(TInt, ThreadCreate, eka2l1::ptr<TDesC8> aThreadName, TOwnerType aOwnerType, eka2l1::ptr<thread_create_info_expand> aInfo) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        // Get rid of null terminator
        std::string thr_name = aThreadName.get(mem)->StdString(sys).c_str();
        thread_create_info_expand *info = aInfo.get(mem);

        uint32_t thr_handle = kern->create_thread(static_cast<kernel::owner_type>(aOwnerType), kern->crr_process(),
            kernel::access_type::local_access, thr_name, info->func_ptr, info->user_stack_size,
            info->heap_initial_size, info->heap_max_size, false, info->ptr, info->allocator, kernel::thread_priority::priority_normal);

        if (thr_handle == INVALID_HANDLE) {
            return KErrGeneral;
        } else {
            LOG_TRACE("Thread {} created with start pc = 0x{:x}, stack size = 0x{:x}", thr_name,
                info->func_ptr, info->user_stack_size);
        }

        return thr_handle;
    }

    BRIDGE_FUNC(TInt, LastThreadHandle) {
        return sys->get_kernel_system()->crr_thread()->last_handle();
    }

    BRIDGE_FUNC(TInt, ThreadID, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        if (!thr) {
            return KErrBadHandle;
        }

        return static_cast<TInt>(thr->unique_id());
    }

    BRIDGE_FUNC(TInt, ThreadKill, TInt aHandle, TExitType aExitType, TInt aReason, eka2l1::ptr<TDesC8> aReasonDes) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        if (!thr) {
            return KErrBadHandle;
        }

        std::string exit_cage = "None";
        std::string thread_name = thr->name();

        if (aReasonDes) {
            exit_cage = aReasonDes.get(mem)->StdString(sys);
        }

        std::optional<std::string> exit_description;

        if (is_panic_category_action_default(exit_cage)) {
            exit_description = get_panic_description(exit_cage, aReason);

            switch (aExitType) {
            case TExitType::panic:
                LOG_TRACE("Thread {} paniced with cagetory: {} and exit code: {} {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            case TExitType::kill:
                LOG_TRACE("Thread {} forcefully killed with cagetory: {} and exit code: {} {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            case TExitType::terminate:
            case TExitType::pending:
                LOG_TRACE("Thread {} terminated peacefully with cagetory: {} and exit code: {}", thread_name, exit_cage, aReason,
                    exit_description ? (std::string("(") + *exit_description + ")") : "");
                break;

            default:
                return KErrArgument;
            }
        }

        if (thr->owning_process()->decrease_thread_count() == 0) {
            thr->owning_process()->set_exit_type(static_cast<kernel::process_exit_type>(aExitType));
        }

#ifdef ENABLE_SCRIPTING
        sys->get_manager_system()->get_script_manager()->call_panics(exit_cage, aReason);
#endif

        kern->get_thread_scheduler()->stop(thr);
        kern->prepare_reschedule();

        return KErrNone;
    }

    BRIDGE_FUNC(void, ThreadRequestSignal, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        thread_ptr thr;

        // Current thread handle
        if (aHandle == 0xFFFF8001) {
            thr = kern->crr_thread();
        } else {
            thr = kern->get_thread_by_handle(aHandle);
        }

        if (!thr) {
            return;
        }

        thr->signal_request();
    }

    BRIDGE_FUNC(TInt, ThreadRename, TInt aHandle, eka2l1::ptr<TDesC8> aName) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        thread_ptr thr;
        TDesC8 *name = aName.get(mem);

        // Current thread handle
        if (aHandle == 0xFFFF8001) {
            thr = kern->crr_thread();
        } else {
            thr = kern->get_thread_by_handle(aHandle);
        }

        if (!thr) {
            return KErrBadHandle;
        }

        std::string new_name = name->StdString(sys);

        LOG_TRACE("Thread with last name: {} renamed to {}", thr->name(), new_name);

        thr->rename(new_name);

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, ThreadProcess, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();

        thread_ptr thr = kern->get_thread_by_handle(aHandle);
        return kern->mirror(thr->owning_process(), kernel::owner_type::thread);
    }

    BRIDGE_FUNC(void, ThreadSetPriority, TInt aHandle, TInt aThreadPriority) {
        kernel_system *kern = sys->get_kernel_system();
        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        if (!thr) {
            return;
        }

        thr->set_priority(static_cast<eka2l1::kernel::thread_priority>(aThreadPriority));
    }

    BRIDGE_FUNC(void, ThreadResume, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        if (!thr) {
            LOG_ERROR("invalid thread handle 0x{:x}", aHandle);
            return;
        }

        switch (thr->current_state()) {
        case kernel::thread_state::create: {
            kern->get_thread_scheduler()->schedule(thr);
            break;
        }

        default: {
            thr->resume();
            break;
        }
        }
    }

    BRIDGE_FUNC(void, ThreadSuspend, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        if (!thr) {
            LOG_ERROR("invalid thread handle 0x{:x}", aHandle);
            return;
        }

        switch (thr->current_state()) {
        case kernel::thread_state::create: {
            break;
        }

        default: {
            thr->suspend();
            break;
        }
        }
    }

    BRIDGE_FUNC(void, ThreadLogon, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestSts, TBool aRendezvous) {
        thread_ptr thr = sys->get_kernel_system()->get_thread_by_handle(aHandle);

        if (!thr) {
            return;
        }

        thr->logon(aRequestSts.get(sys->get_kernel_system()->get_memory_system()), aRendezvous);
    }

    BRIDGE_FUNC(TInt, ThreadLogonCancel, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestSts, TBool aRendezvous) {
        thread_ptr thr = sys->get_kernel_system()->get_thread_by_handle(aHandle);

        if (!thr) {
            return KErrBadHandle;
        }

        bool logon_success = thr->logon_cancel(aRequestSts.get(sys->get_kernel_system()->get_memory_system()), aRendezvous);

        if (logon_success) {
            return KErrNone;
        }

        return KErrGeneral;
    }

    BRIDGE_FUNC(void, ThreadSetFlags, TInt aHandle, TUint aClearMask, TUint aSetMask) {
        kernel_system *kern = sys->get_kernel_system();

        thread_ptr thr = kern->get_thread_by_handle(aHandle);

        uint32_t org_flags = thr->get_flags();
        uint32_t new_flags = ((org_flags & ~aClearMask) | aSetMask);
        new_flags = (new_flags ^ org_flags);

        thr->set_flags(org_flags ^ new_flags);
    }

    /*****************************/
    /* PROPERTY */
    /****************************/

    BRIDGE_FUNC(TInt, PropertyFindGetInt, TInt aCage, TInt aKey, eka2l1::ptr<TInt> aValue) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        property_ptr prop = kern->get_prop(aCage, aKey);

        if (!prop) {
            LOG_WARN("Property not found: cagetory = 0x{:x}, key = 0x{:x}", aCage, aKey);
            return KErrNotFound;
        }

        TInt *val_ptr = aValue.get(mem);
        *val_ptr = prop->get_int();

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyFindGetBin, TInt aCage, TInt aKey, eka2l1::ptr<TUint8> aData, TInt aDataLength) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        property_ptr prop = kern->get_prop(aCage, aKey);

        if (!prop) {
            LOG_WARN("Property not found: cagetory = 0x{:x}, key = 0x{:x}", aCage, aKey);
            return KErrNotFound;
        }

        TUint8 *data_ptr = aData.get(mem);
        auto data_vec = prop->get_bin();

        if (data_vec.size() > aDataLength) {
            return KErrNoMemory;
        }

        memcpy(data_ptr, data_vec.data(), data_vec.size());

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyAttach, TInt aCagetory, TInt aValue, TOwnerType aOwnerType) {
        kernel_system *kern = sys->get_kernel_system();
        property_ptr prop = kern->get_prop(aCagetory, aValue);

        LOG_TRACE("Attach to property with cagetory: 0x{:x}, key: 0x{:x}", aCagetory, aValue);

        if (!prop) {
            uint32_t property_handle = kern->create_prop(static_cast<kernel::owner_type>(aOwnerType));

            if (property_handle == INVALID_HANDLE) {
                return KErrGeneral;
            }

            prop = kern->get_prop(property_handle);

            prop->first = aCagetory;
            prop->second = aValue;

            return property_handle;
        }

        return kern->mirror(prop, static_cast<kernel::owner_type>(aOwnerType));
    }

    BRIDGE_FUNC(TInt, PropertyDefine, TInt aCagetory, TInt aKey, eka2l1::ptr<TPropertyInfo> aPropertyInfo) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TPropertyInfo *info = aPropertyInfo.get(mem);

        service::property_type prop_type;

        switch (info->iType) {
        case EInt:
            prop_type = service::property_type::int_data;
            break;

        case ELargeByteArray:
        case EByteArray:
            prop_type = service::property_type::bin_data;
            break;

        default: {
            LOG_WARN("Unknown property type, exit with KErrGeneral.");
            return KErrArgument;
        }
        }

        LOG_TRACE("Define to property with cagetory: 0x{:x}, key: 0x{:x}, type: {}", aCagetory, aKey,
            prop_type == service::property_type::int_data ? "int" : "bin");

        property_ptr prop = kern->get_prop(aCagetory, aKey);

        if (!prop) {
            uint32_t property_handle = kern->create_prop();

            if (property_handle == INVALID_HANDLE) {
                return KErrGeneral;
            }

            prop = kern->get_prop(property_handle);

            prop->first = aCagetory;
            prop->second = aKey;
        }

        prop->define(prop_type, info->iSize);

        return KErrNone;
    }

    BRIDGE_FUNC(void, PropertySubscribe, TInt aPropertyHandle, eka2l1::ptr<epoc::request_status> aRequestStatus) {
        kernel_system *kern = sys->get_kernel_system();
        property_ptr prop = kern->get_prop(aPropertyHandle);

        if (!prop) {
            return;
        }

        prop->subscribe(aRequestStatus.get(sys->get_memory_system()));

        return;
    }

    BRIDGE_FUNC(void, PropertyCancel, TInt aPropertyHandle) {
        kernel_system *kern = sys->get_kernel_system();
        property_ptr prop = kern->get_prop(aPropertyHandle);

        if (!prop) {
            return;
        }

        prop->cancel();

        return;
    }

    BRIDGE_FUNC(TInt, PropertySetInt, TInt aHandle, TInt aValue) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aHandle);

        if (!prop) {
            return KErrBadHandle;
        }

        bool res = prop->set_int(aValue);

        if (!res) {
            return KErrArgument;
        }

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertySetBin, TInt aHandle, TInt aSize, eka2l1::ptr<TUint8> aDataPtr) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aHandle);

        if (!prop) {
            return KErrBadHandle;
        }

        bool res = prop->set(aDataPtr.get(mem), aSize);

        if (!res) {
            return KErrArgument;
        }

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyGetInt, TInt aHandle, eka2l1::ptr<TInt> aValuePtr) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aHandle);

        if (!prop) {
            return KErrBadHandle;
        }

        *aValuePtr.get(mem) = prop->get_int();

        if (prop->get_int() == -1) {
            return KErrArgument;
        }

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyGetBin, TInt aHandle, TInt aSize, eka2l1::ptr<TUint8> aDataPtr) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aHandle);

        if (!prop) {
            return KErrBadHandle;
        }

        std::vector<uint8_t> dat = prop->get_bin();

        if (dat.size() == 0) {
            return KErrArgument;
        }

        if (dat.size() < aSize) {
            return KErrNoMemory;
        }

        std::copy(dat.begin(), dat.end(), aDataPtr.get(mem));

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyFindSetInt, TInt aCage, TInt aKey, TInt aValue) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aCage, aKey);

        if (!prop) {
            return KErrBadHandle;
        }

        bool res = prop->set(aValue);

        if (!res) {
            return KErrArgument;
        }

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, PropertyFindSetBin, TInt aCage, TInt aKey, TInt aSize, eka2l1::ptr<TUint8> aDataPtr) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        property_ptr prop = kern->get_prop(aCage, aKey);

        if (!prop) {
            return KErrBadHandle;
        }

        bool res = prop->set(aDataPtr.get(mem), aSize);

        if (!res) {
            return KErrArgument;
        }

        return KErrNone;
    }

    /**********************/
    /* TIMER */
    /*********************/
    BRIDGE_FUNC(TInt, TimerCreate) {
        return sys->get_kernel_system()->create_timer("timer" + common::to_string(eka2l1::random()),
            kernel::owner_type::process);
    }

    /* 
    * Note: the difference between At and After on hardware is At request still actives when the phone shutdown.
    * At is extremely important to implement the alarm in S60 (i believe S60v4 is a part based on S60 so it maybe related).
    * In emulator, it's the same, so i implement it as TimerAffter.
    */

    BRIDGE_FUNC(void, TimerAfter, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestStatus, TInt aMicroSeconds) {
        kernel_system *kern = sys->get_kernel_system();
        timer_ptr timer = std::dynamic_pointer_cast<kernel::timer>(kern->get_kernel_obj(aHandle));

        if (!timer) {
            return;
        }

        timer->after(kern->crr_thread(), aRequestStatus.get(sys->get_memory_system()), aMicroSeconds);
    }

    BRIDGE_FUNC(void, TimerAtUtc, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestStatus, TUint64 aMicroSecondsAt) {
        kernel_system *kern = sys->get_kernel_system();
        timer_ptr timer = std::dynamic_pointer_cast<kernel::timer>(kern->get_kernel_obj(aHandle));

        if (!timer) {
            return;
        }

        timer->after(kern->crr_thread(), aRequestStatus.get(sys->get_memory_system()), aMicroSecondsAt - std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - ad_epoc_dist_microsecs);
    }

    BRIDGE_FUNC(void, TimerCancel, TInt aHandle) {
        kernel_system *kern = sys->get_kernel_system();
        timer_ptr timer = std::dynamic_pointer_cast<kernel::timer>(kern->get_kernel_obj(aHandle));

        if (!timer) {
            return;
        }

        timer->cancel_request();
    }

    /**********************/
    /* CHANGE NOTIFIER */
    /**********************/
    BRIDGE_FUNC(TInt, ChangeNotifierCreate, TOwnerType aOwner) {
        return sys->get_kernel_system()->create_change_notifier(static_cast<kernel::owner_type>(aOwner));
    }

    BRIDGE_FUNC(TInt, ChangeNotifierLogon, TInt aHandle, eka2l1::ptr<epoc::request_status> aRequestStatus) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        change_notifier_ptr cnot = std::dynamic_pointer_cast<kernel::change_notifier>(kern->get_kernel_obj(aHandle));

        if (!cnot) {
            return KErrBadHandle;
        }

        bool res = cnot->logon(aRequestStatus);

        if (!res) {
            return KErrGeneral;
        }

        return KErrNone;
    }

    /* DEBUG AND SECURITY */

    BRIDGE_FUNC(void, DebugPrint, eka2l1::ptr<TDesC8> aDes, TInt aMode) {
        LOG_TRACE("{}", aDes.get(sys->get_memory_system())->StdString(sys));
    }

    // Let all pass for now
    BRIDGE_FUNC(TInt, PlatSecDiagnostic, eka2l1::ptr<TAny> aPlatSecInfo) {
        return KErrNone;
    }

    BRIDGE_FUNC(eka2l1::ptr<void>, GetGlobalUserData) {
        LOG_INFO("GetGlobalUserData stubbed with zero");
        return 0;
    }

    BRIDGE_FUNC(address, ExceptionDescriptor, address aInAddr) {
        return epoc::get_exception_descriptor_addr(sys, aInAddr);
    }

    /* ATOMIC OPERATION */
    /* TODO: Use host atomic function when multi-core available */    
    struct SAtomicOpInfo32
    {
        TAny*		iA;
        union
        {
            TAny*	iQ;
            TUint32	i0;
        };

        TUint32		i1;
        TUint32		i2;
    };

    /*! \brief Increase value by 1 if it's positive (> 0)
        \returns Original value
    */
    BRIDGE_FUNC(TInt32, SafeInc32, eka2l1::ptr<TInt32> aVal)
    {
        TInt32 *val = aVal.get(sys->get_memory_system());
        TInt32 org_val = *val;
        *val > 0 ? val++ : 0;

        return org_val;
    }

    BRIDGE_FUNC(TInt32, SafeDec32, eka2l1::ptr<TInt32> aVal)
    {
        TInt32 *val = aVal.get(sys->get_memory_system());
        TInt32 org_val = *val;
        *val > 0 ? val-- : 0;

        return org_val;
    }

    BRIDGE_FUNC(TInt32, AtomicTas32, eka2l1::ptr<SAtomicOpInfo32> aAtomicInfo) {
        SAtomicOpInfo32 *info = aAtomicInfo.get(sys->get_memory_system());

        TInt32 *A = reinterpret_cast<TInt32*>(info->iA);
        TInt32 old = *A;

        (*A >= info->i0) ? (*A += info->i1) : (*A += info->i2);

        return old;
    }

    const eka2l1::hle::func_map svc_register_funcs_v94 = {
        /* FAST EXECUTIVE CALL */
        BRIDGE_REGISTER(0x00800000, WaitForAnyRequest),
        BRIDGE_REGISTER(0x00800001, Heap),
        BRIDGE_REGISTER(0x00800002, HeapSwitch),
        BRIDGE_REGISTER(0x00800005, ActiveScheduler),
        BRIDGE_REGISTER(0x00800006, SetActiveScheduler),
        BRIDGE_REGISTER(0x00800008, TrapHandler),
        BRIDGE_REGISTER(0x00800009, SetTrapHandler),
        BRIDGE_REGISTER(0x0080000C, DebugMask),
        BRIDGE_REGISTER(0x0080000D, DebugMaskIndex),
        BRIDGE_REGISTER(0x00800013, UserSvrRomHeaderAddress),
        BRIDGE_REGISTER(0x00800015, SafeInc32),
        BRIDGE_REGISTER(0x00800019, UTCOffset),
        BRIDGE_REGISTER(0x0080001A, GetGlobalUserData),
        /* SLOW EXECUTIVE CALL */
        BRIDGE_REGISTER(0x00, ObjectNext),
        BRIDGE_REGISTER(0x01, ChunkBase),
        BRIDGE_REGISTER(0x03, ChunkMaxSize),
        BRIDGE_REGISTER(0x0E, LibraryLookup),
        BRIDGE_REGISTER(0x13, ProcessGetId),
        BRIDGE_REGISTER(0x14, DllFileName),
        BRIDGE_REGISTER(0x15, ProcessResume),
        BRIDGE_REGISTER(0x16, ProcessFilename),
        BRIDGE_REGISTER(0x17, ProcessCommandLine),
        BRIDGE_REGISTER(0x18, ProcessExitType),
        BRIDGE_REGISTER(0x1C, ProcessSetPriority),
        BRIDGE_REGISTER(0x1E, ProcessSetFlags),
        BRIDGE_REGISTER(0x1F, SemaphoreWait),
        BRIDGE_REGISTER(0x20, SemaphoreSignal),
        BRIDGE_REGISTER(0x21, SemaphoreSignalN),
        BRIDGE_REGISTER(0x22, ServerReceive),
        BRIDGE_REGISTER(0x23, ServerCancel),
        BRIDGE_REGISTER(0x24, SetSessionPtr),
        BRIDGE_REGISTER(0x25, SessionSend),
        BRIDGE_REGISTER(0x26, ThreadID),
        BRIDGE_REGISTER(0x27, SessionShare),
        BRIDGE_REGISTER(0x28, ThreadResume),
        BRIDGE_REGISTER(0x29, ThreadSuspend),
        BRIDGE_REGISTER(0x2B, ThreadSetPriority),
        BRIDGE_REGISTER(0x2F, ThreadSetFlags),
        BRIDGE_REGISTER(0x35, TimerCancel),
        BRIDGE_REGISTER(0x36, TimerAfter),
        BRIDGE_REGISTER(0x37, TimerAtUtc),
        BRIDGE_REGISTER(0x39, ChangeNotifierLogon),
        BRIDGE_REGISTER(0x3B, RequestSignal),
        BRIDGE_REGISTER(0x3C, HandleName),
        BRIDGE_REGISTER(0x40, After),
        BRIDGE_REGISTER(0x42, MessageComplete),
        BRIDGE_REGISTER(0x44, TimeNow),
        BRIDGE_REGISTER(0x4D, SessionSendSync),
        BRIDGE_REGISTER(0x4E, DllTls),
        BRIDGE_REGISTER(0x4F, HalFunction),
        BRIDGE_REGISTER(0x52, ProcessCommandLineLength),
        BRIDGE_REGISTER(0x56, DebugPrint),
        BRIDGE_REGISTER(0x6A, HandleClose),
        BRIDGE_REGISTER(0x64, ProcessType),
        BRIDGE_REGISTER(0x68, ThreadCreate),
        BRIDGE_REGISTER(0x6B, ChunkCreate),
        BRIDGE_REGISTER(0x6C, ChunkAdjust),
        BRIDGE_REGISTER(0x6D, HandleOpenObject),
        BRIDGE_REGISTER(0x6E, HandleDuplicate),
        BRIDGE_REGISTER(0x6F, MutexCreate),
        BRIDGE_REGISTER(0x70, SemaphoreCreate),
        BRIDGE_REGISTER(0x73, ThreadKill),
        BRIDGE_REGISTER(0x74, ThreadLogon),
        BRIDGE_REGISTER(0x75, ThreadLogonCancel),
        BRIDGE_REGISTER(0x76, DllSetTls),
        BRIDGE_REGISTER(0x77, DllFreeTLS),
        BRIDGE_REGISTER(0x78, ThreadRename),
        BRIDGE_REGISTER(0x7B, ProcessLogon),
        BRIDGE_REGISTER(0x7C, ProcessLogonCancel),
        BRIDGE_REGISTER(0x7D, ThreadProcess),
        BRIDGE_REGISTER(0x7E, ServerCreate),
        BRIDGE_REGISTER(0x7F, SessionCreate),
        BRIDGE_REGISTER(0x84, TimerCreate),
        BRIDGE_REGISTER(0x87, ChangeNotifierCreate),
        BRIDGE_REGISTER(0x9C, WaitDllLock),
        BRIDGE_REGISTER(0x9D, ReleaseDllLock),
        BRIDGE_REGISTER(0x9E, LibraryAttach),
        BRIDGE_REGISTER(0x9F, LibraryAttached),
        BRIDGE_REGISTER(0xA0, StaticCallList),
        BRIDGE_REGISTER(0xA3, LastThreadHandle),
        BRIDGE_REGISTER(0xA5, ProcessRendezvous),
        BRIDGE_REGISTER(0xA6, MessageGetDesLength),
        BRIDGE_REGISTER(0xA7, MessageGetDesMaxLength),
        BRIDGE_REGISTER(0xA8, MessageIpcCopy),
        BRIDGE_REGISTER(0xAC, MessageKill),
        BRIDGE_REGISTER(0xAE, ProcessSecurityInfo),
        BRIDGE_REGISTER(0xAF, ThreadSecurityInfo),
        BRIDGE_REGISTER(0xB0, MessageSecurityInfo),
        BRIDGE_REGISTER(0xBC, PropertyDefine),
        BRIDGE_REGISTER(0xBE, PropertyAttach),
        BRIDGE_REGISTER(0xBF, PropertySubscribe),
        BRIDGE_REGISTER(0xC0, PropertyCancel),
        BRIDGE_REGISTER(0xC1, PropertyGetInt),
        BRIDGE_REGISTER(0xC2, PropertyGetBin),
        BRIDGE_REGISTER(0xC3, PropertySetInt),
        BRIDGE_REGISTER(0xC4, PropertySetBin),
        BRIDGE_REGISTER(0xC5, PropertyFindGetInt),
        BRIDGE_REGISTER(0xC6, PropertyFindGetBin),
        BRIDGE_REGISTER(0xC7, PropertyFindSetInt),
        BRIDGE_REGISTER(0xC8, PropertyFindSetBin),
        BRIDGE_REGISTER(0xCF, ProcessSetDataParameter),
        BRIDGE_REGISTER(0xD1, ProcessGetDataParameter),
        BRIDGE_REGISTER(0xD2, ProcessDataParameterLength),
        BRIDGE_REGISTER(0xDB, PlatSecDiagnostic),
        BRIDGE_REGISTER(0xDC, ExceptionDescriptor),
        BRIDGE_REGISTER(0xDD, ThreadRequestSignal),
        BRIDGE_REGISTER(0xDF, LeaveStart),
        BRIDGE_REGISTER(0xE0, LeaveEnd)
    };

    const eka2l1::hle::func_map svc_register_funcs_v93 = {
        /* FAST EXECUTIVE CALL */
        BRIDGE_REGISTER(0x00800000, WaitForAnyRequest),
        BRIDGE_REGISTER(0x00800001, Heap),
        BRIDGE_REGISTER(0x00800002, HeapSwitch),
        BRIDGE_REGISTER(0x00800005, ActiveScheduler),
        BRIDGE_REGISTER(0x00800006, SetActiveScheduler),
        BRIDGE_REGISTER(0x00800008, TrapHandler),
        BRIDGE_REGISTER(0x00800009, SetTrapHandler),
        BRIDGE_REGISTER(0x0080000D, DebugMask),
        /* SLOW EXECUTIVE CALL */
        BRIDGE_REGISTER(0x00, ObjectNext)
    };
}