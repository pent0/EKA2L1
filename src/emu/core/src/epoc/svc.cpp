#include <epoc/chunk.h>
#include <epoc/dll.h>
#include <epoc/hal.h>
#include <epoc/handle.h>
#include <epoc/svc.h>
#include <epoc/tl.h>
#include <epoc/uid.h>

#include <common/cvt.h>
#include <common/path.h>

#ifdef WIN32
#include <Windows.h>
#endif

#include <chrono>
#include <ctime>

namespace eka2l1::epoc {
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

    /****************************/
    /* PROCESS */
    /***************************/

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
            pr_real = kern->get_process(kern->crr_process());
        }

        if (!pr_real) {
            LOG_ERROR("SvcProcessFileName: Invalid process");
            return;
        }

        TDes8 *des = aDes.get(mem);
        des->Assign(sys, pr_real->name());
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
            pr_real = kern->get_process(kern->crr_process());
        }

        if (!pr_real) {
            LOG_ERROR("SvcProcessType: Invalid process");
            return;
        }

        TUidType *type = uid_type.get(mem);
        auto &tup = pr_real->get_uid_type();

        type->uid1 = std::get<0>(tup);
        type->uid2 = std::get<1>(tup);
        type->uid3 = std::get<2>(tup);
    }

    BRIDGE_FUNC(TInt, ProcessDataParameterLength, TInt aSlot) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr crr_process = kern->get_process(kern->crr_process());

        auto slot = crr_process->get_arg_slot(aSlot);

        if (!slot) {
            return KErrNotFound;
        }

        return static_cast<TInt>(slot->data_size);
    }

    BRIDGE_FUNC(TInt, ProcessGetDataParameter, TInt aSlot, eka2l1::ptr<TUint8> aData, TInt aLength) {
        kernel_system *kern = sys->get_kernel_system();
        process_ptr pr = kern->get_process(kern->crr_process());

        if (aSlot >= 16 || aSlot < 0) {
            LOG_ERROR("Invalid slot (slot: {} >= 16 or < 0)", aSlot);
            return KErrGeneral;
        }

        auto slot = *pr->get_arg_slot(aSlot);

        if (slot.data_size == -1) {
            return KErrNotFound;
        }

        if (aLength < slot.data_size) {
            return KErrArgument;
        }

        TUint8 *data = aData.get(sys->get_memory_system());

        if (aSlot == 1) {
            std::u16string arg = u"\0l" + pr->get_exe_path();

            if (!pr->get_cmd_args().empty()) {
                arg += u" " + pr->get_cmd_args();
            }

            char src = 0x00;
            char src2 = 0x6C;

            memcpy(data + 2, common::ucs2_to_utf8(arg).data(), arg.length());
            memcpy(data, &src, 1);
            memcpy(data + 1, &src2, 1);

            return slot.data_size;
        }

        TUint8 *ptr2 = eka2l1::ptr<TUint8>(slot.data).get(sys->get_memory_system());
        memcpy(data, ptr2, slot.data_size);

        return slot.data_size;
    }

    /********************/
    /* TLS */
    /*******************/

    BRIDGE_FUNC(eka2l1::ptr<void>, DllTls, TInt aHandle, TInt aDllUid) {
        eka2l1::kernel::thread_local_data dat = current_local_data(sys);

        for (const auto &tls : dat.tls_slots) {
            if (tls.handle == aHandle) {
                return tls.ptr;
            }
        }

        return eka2l1::ptr<void>(nullptr);
    }

    BRIDGE_FUNC(TInt, DllSetTls, TInt aHandle, TInt aDllUid, eka2l1::ptr<void> aPtr) {
        eka2l1::kernel::tls_slot *slot = get_tls_slot(sys, aHandle);

        if (!slot) {
            return KErrNoMemory;
        }

        slot->ptr = aPtr;

        return KErrNone;
    }

    BRIDGE_FUNC(void, DllFreeTLS, TInt iHandle) {
        thread_ptr thr = sys->get_kernel_system()->crr_thread();
        thr->close_tls_slot(*thr->get_tls_slot(iHandle, iHandle));
    }

    /***********************************/
    /* LOCALE */
    /**********************************/

    BRIDGE_FUNC(TInt, UTCOffset) {
        // Stubbed
        return -14400;
    }

    /********************************************/
    /* IPC */
    /*******************************************/

    BRIDGE_FUNC(TInt, SessionCreate, eka2l1::ptr<TDesC8> aServerName, TInt aMsgSlot, eka2l1::ptr<void> aSec, TInt aMode) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        std::string server_name = aServerName.get(mem)->StdString(sys);
        server_ptr server = kern->get_server_by_name(server_name);

        if (!server) {
            return KErrNotFound;
        }

        session_ptr session = kern->create_session(server, aMsgSlot);

        if (!session) {
            return KErrGeneral;
        }

        LOG_TRACE("New session connected to {} with id {}", server_name, session->unique_id());

        return session->unique_id();
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
        *handle = kern->mirror(*handle, kernel::owner_type::process);
        kern->close(old_handle);

        LOG_TRACE("Old handle: {}, new handle: {}", (old_handle & 0x8000) ? (old_handle & ~0x8000) : (old_handle),
            *handle);

        return KErrNone;
    }

    BRIDGE_FUNC(TInt, SessionSendSync, TInt aHandle, TInt aOrd, eka2l1::ptr<TAny> aIpcArgs,
        eka2l1::ptr<TInt> aStatus) {
        
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

        return ss->send_receive_sync(aOrd, arg, aStatus.get(mem));
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

    BRIDGE_FUNC(TInt, DebugMask) {
        return 96; // Constant debug mask
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

    BRIDGE_FUNC(TInt, ChunkCreate, TOwnerType aOwnerType, eka2l1::ptr<eka2l1::epoc::TDesC8> aName, eka2l1::ptr<TChunkCreate> aChunkCreate) {
        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        TChunkCreate createInfo = *aChunkCreate.get(mem);
        TDesC8 name = *aName.get(mem);

        auto lol = name.StdString(sys);

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

        if (access == decltype(access)::global && name.Length() == 0) {
            att = kernel::chunk_attrib::anonymous;
        }

        chunk_ptr chunk = kern->create_chunk(name.StdString(sys), createInfo.iInitialBottom, createInfo.iInitialTop,
            createInfo.iMaxSize, prot::read_write, type, access, att,
            aOwnerType == EOwnerProcess ? kernel::owner_type::process : kernel::owner_type::thread);

        if (!chunk) {
            return KErrNoMemory;
        }

        return chunk->unique_id();
    }

    BRIDGE_FUNC(TInt, ChunkMaxSize, TInt aChunkHandle) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return KErrBadHandle;
        }

        chunk_ptr chunk = std::reinterpret_pointer_cast<kernel::chunk>(obj);
        return chunk->get_max_size();
    }

    BRIDGE_FUNC(eka2l1::ptr<TUint8>, ChunkBase, TInt aChunkHandle) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return nullptr;
        }

        chunk_ptr chunk = std::reinterpret_pointer_cast<kernel::chunk>(obj);
        return chunk->base();
    }

    BRIDGE_FUNC(TInt, ChunkAdjust, TInt aChunkHandle, TInt aType, TInt a1, TInt a2) {
        kernel_obj_ptr obj = RHandleBase(aChunkHandle).GetKObject(sys);

        if (!obj) {
            return KErrBadHandle;
        }

        chunk_ptr chunk = std::reinterpret_pointer_cast<kernel::chunk>(obj);

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
                return KErrNone;
            }

            return KErrGeneral;
        };

        bool res = fetch(chunk, a1, a2);

        if (!res)
            return KErrGeneral;

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

        sema_ptr sema = kern->create_sema(!desname ? "" : desname->StdString(sys),
            aInitCount, 50, owner,
            kern->get_id_base_owner(owner), !desname ? kernel::access_type::local_access : kernel::access_type::global_access);

        if (!sema) {
            return KErrGeneral;
        }

        return sema->unique_id();
    }

    BRIDGE_FUNC(void, WaitForAnyRequest) {
        sys->get_kernel_system()->crr_thread()->wait_for_any_request();
    }

    /***********************************************/
    /* HANDLE FUNCTIONS   */
    /*                    */
    /* Thread independent */
    /**********************************************/

    BRIDGE_FUNC(TInt, HandleClose, TInt aHandle) {
        if (aHandle & 0x8000) {
            return false;
        }

        bool res = sys->get_kernel_system()->close(aHandle);

        if (res) {
            return KErrNone;
        }

        return KErrBadHandle;
    }

    BRIDGE_FUNC(TInt, HandleDuplicate, TInt aThreadHandle, TOwnerType aOwnerType, TInt aDupHandle) {
        if (aDupHandle & 0x8000) {
            aDupHandle &= ~0x8000;
        }

        memory_system *mem = sys->get_memory_system();
        kernel_system *kern = sys->get_kernel_system();

        return kern->mirror(aDupHandle,
            (aOwnerType == EOwnerProcess) ? kernel::owner_type::process : kernel::owner_type::thread);
    }


    BRIDGE_FUNC(TInt, HandleOpenObject, TObjectType aObjectType, eka2l1::ptr<epoc::TDesC8> aName, TInt aOwnerType) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        std::string obj_name = aName.get(mem)->StdString(sys);

        switch (aObjectType) {
        case EChunk:
            return kern->mirror_chunk(obj_name,
                (aOwnerType == EOwnerProcess ? kernel::owner_type::process : kernel::owner_type::thread));
        }

        return KErrGeneral;
    }

    BRIDGE_FUNC(void, HandleName, TInt aHandle, eka2l1::ptr<TDes8> aName) {
        kernel_system *kern = sys->get_kernel_system();
        kernel_obj_ptr obj = kern->get_kernel_obj(aHandle);

        if (!obj) {
            if (aHandle == 0xFFFF8001) {
                obj = kern->crr_thread();
            } else
                return;
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

        *total = list.size();
        memcpy(list_ptr, list.data(), sizeof(TUint32) * *total);

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

    BRIDGE_FUNC(TInt, ThreadRename, TInt aHandle, eka2l1::ptr<TDesC8> aName) {
        kernel_system *kern = sys->get_kernel_system();
        memory_system *mem = sys->get_memory_system();

        thread_ptr thr;
        TDesC8 *name = aName.get(mem);

        // Current thread handle
        if (aHandle == 0xFFFF8001) {
            thr = kern->crr_thread();
        } else {
            thr = kern->get_thread_by_id(aHandle);
        }

        if (!thr) {
            return KErrBadHandle;
        }

        std::string new_name = name->StdString(sys);
        thr->rename(new_name);

        return KErrNone;
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

    const eka2l1::hle::func_map svc_register_funcs_v94 = {
        /* FAST EXECUTIVE CALL */
        BRIDGE_REGISTER(0x00800000, WaitForAnyRequest),
        BRIDGE_REGISTER(0x00800001, Heap),
        BRIDGE_REGISTER(0x00800002, HeapSwitch),
        BRIDGE_REGISTER(0x00800005, ActiveScheduler),
        BRIDGE_REGISTER(0x00800006, SetActiveScheduler),
        BRIDGE_REGISTER(0x00800008, TrapHandler),
        BRIDGE_REGISTER(0x00800009, SetTrapHandler),
        BRIDGE_REGISTER(0x0080000D, DebugMask),
        BRIDGE_REGISTER(0x00800013, UserSvrRomHeaderAddress),
        BRIDGE_REGISTER(0x00800019, UTCOffset),
        /* SLOW EXECUTIVE CALL */
        BRIDGE_REGISTER(0x01, ChunkBase),
        BRIDGE_REGISTER(0x03, ChunkMaxSize),
        BRIDGE_REGISTER(0x16, ProcessFilename),
        BRIDGE_REGISTER(0x27, SessionShare),
        BRIDGE_REGISTER(0x3C, HandleName),
        BRIDGE_REGISTER(0x4D, SessionSendSync),
        BRIDGE_REGISTER(0x4E, DllTls),
        BRIDGE_REGISTER(0x4F, HalFunction),
        BRIDGE_REGISTER(0x6A, HandleClose),
        BRIDGE_REGISTER(0x64, ProcessType),
        BRIDGE_REGISTER(0x6B, ChunkCreate),
        BRIDGE_REGISTER(0x6C, ChunkAdjust),
        BRIDGE_REGISTER(0x6D, HandleOpenObject),
        BRIDGE_REGISTER(0x6E, HandleDuplicate),
        BRIDGE_REGISTER(0x70, SemaphoreCreate),
        BRIDGE_REGISTER(0x76, DllSetTls),
        BRIDGE_REGISTER(0x77, DllFreeTLS),
        BRIDGE_REGISTER(0x78, ThreadRename),
        BRIDGE_REGISTER(0x7F, SessionCreate),
        BRIDGE_REGISTER(0xA0, StaticCallList),
        BRIDGE_REGISTER(0xC5, PropertyFindGetInt),
        BRIDGE_REGISTER(0xC6, PropertyFindGetBin),
        BRIDGE_REGISTER(0xD1, ProcessGetDataParameter),
        BRIDGE_REGISTER(0xD2, ProcessDataParameterLength),
        BRIDGE_REGISTER(0xDF, LeaveStart)
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
        BRIDGE_REGISTER(0x0080000D, DebugMask)
    };
}