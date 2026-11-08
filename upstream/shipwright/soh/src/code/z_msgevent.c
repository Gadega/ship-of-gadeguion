#include "global.h"

void MsgEvent_SendNullTask(void) {
    s32 pad[4];
    OSScTask task;
    OSMesgQueue queue;
    OSMesg msg;
    u32 pad2[1];

#ifdef __PS3__
    // No RCP scheduler thread on PS3: nothing would ever answer the null task, so
    // the wait below blocked forever (pause menu: Play_Draw R_PAUSE_MENU_MODE 2).
    // Fast3D has already run the previous frame by the time this is called.
    (void)pad;
    (void)pad2;
    (void)task;
    (void)queue;
    (void)msg;
    return;
#endif
    task.next = NULL;
    task.flags = OS_SC_RCP_MASK;
    task.msgQ = &queue;
    task.msg.ptr = NULL;
    task.framebuffer = NULL;
    task.list.t.type = M_NULTASK;
    osCreateMesgQueue(task.msgQ, &msg, 1);
    osSendMesgPtr(&gSchedContext.cmdQ, &task, OS_MESG_BLOCK);
    Sched_SendEntryMsg(&gSchedContext);
    osRecvMesg(&queue, NULL, OS_MESG_BLOCK);
}
