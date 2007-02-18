/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/kd64/kdtrap.c
 * PURPOSE:         KD64 Trap Handlers
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
KdpReport(IN PKTRAP_FRAME TrapFrame,
          IN PKEXCEPTION_FRAME ExceptionFrame,
          IN PEXCEPTION_RECORD ExceptionRecord,
          IN PCONTEXT ContextRecord,
          IN KPROCESSOR_MODE PreviousMode,
          IN BOOLEAN SecondChanceException)
{
    BOOLEAN Entered, Status;
    PKPRCB Prcb;
    while (TRUE);

    /*
     * Only go ahead with this if this is an INT3 or an INT1, or if the global
     * flag forces us to call up the debugger on exception, or if this is a
     * second chance exception which means it hasn't been handled by now.
     */
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) ||
        (ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP)  ||
        (NtGlobalFlag & FLG_STOP_ON_EXCEPTION) ||
        (SecondChanceException))
    {
        /*
         * Also, unless this is a second chance exception, then do not call up
         * the debugger if the debug port is disconnected or the exception code
         * indicates success.
         */
        if (!(SecondChanceException) &&
            ((ExceptionRecord->ExceptionCode == STATUS_PORT_DISCONNECTED) ||
             (NT_SUCCESS(ExceptionRecord->ExceptionCode))))
        {
            /* Return false to hide the exception */
            return FALSE;
        }

        /* Enter the debugger */
        Entered = KdEnterDebugger(TrapFrame, ExceptionFrame);

        /*
         * Get the KPRCB and save the CPU Control State manually instead of
         * using KiSaveProcessorState, since we already have a valid CONTEXT.
         */
        Prcb = KeGetCurrentPrcb();
        KiSaveProcessorControlState(&Prcb->ProcessorState);
        RtlCopyMemory(&Prcb->ProcessorState.ContextFrame,
                      ContextRecord,
                      sizeof(CONTEXT));

        /* Report the new state */
#if 0
        Status = KdpReportExceptionStateChange(ExceptionRecord,
                                               &Prcb->ProcessorState.
                                               ContextFrame,
                                               SecondChanceException);
#else
        Status = FALSE;
#endif

        /* Now restore the processor state, manually again. */
        RtlCopyMemory(ContextRecord,
                      &Prcb->ProcessorState.ContextFrame,
                      sizeof(CONTEXT));
        KiRestoreProcessorControlState(&Prcb->ProcessorState);

        /* Exit the debugger and clear the CTRL-C state */
        KdExitDebugger(Entered);
        KdpControlCPressed = FALSE;
        return Status;
    }

    /* Fail if we got here */
    return FALSE;
}

BOOLEAN
NTAPI
KdpTrap(IN PKTRAP_FRAME TrapFrame,
        IN PKEXCEPTION_FRAME ExceptionFrame,
        IN PEXCEPTION_RECORD ExceptionRecord,
        IN PCONTEXT ContextRecord,
        IN KPROCESSOR_MODE PreviousMode,
        IN BOOLEAN SecondChanceException)
{
    BOOLEAN Unload = FALSE;
    ULONG Eip, Eax;
    BOOLEAN Status = FALSE;
    while (TRUE);

    /*
     * Check if we got a STATUS_BREAKPOINT with a SubID for Print, Prompt or
     * Load/Unload symbols.
     */
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->ExceptionInformation[0] != BREAKPOINT_BREAK))
    {
        /* Save EIP */
        Eip = ContextRecord->Eip;

        /* Check what kind of operation was requested from us */
        switch (ExceptionRecord->ExceptionInformation[0])
        {
            /* DbgPrint */
            case BREAKPOINT_PRINT:

                /* Call the worker routine */
                Eax = 0;

                /* Update the return value for the caller */
                ContextRecord->Eax = Eax;
                break;

            /* DbgPrompt */
            case BREAKPOINT_PROMPT:

                /* Call the worker routine */
                Eax = 0;
                Status = TRUE;

                /* Update the return value for the caller */
                ContextRecord->Eax = Eax;
                break;

            /* DbgUnloadSymbols */
            case BREAKPOINT_UNLOAD_SYMBOLS:

                /* Drop into the load case below, with the unload parameter */
                Unload = TRUE;

            /* DbgLoadSymbols */
            case BREAKPOINT_LOAD_SYMBOLS:

                /* Call the worker routine */
                Status = TRUE;
                break;

            /* DbgCommandString*/
            case BREAKPOINT_COMMAND_STRING:

                /* Call the worker routine */
                Status = TRUE;

            /* Anything else, do nothing */
            default:

                /* Get out */
                break;
        }

        /*
         * If EIP was not updated, we'll increment it ourselves so execution
         * continues past the breakpoint.
         */
        if (ContextRecord->Eip == Eip) ContextRecord->Eip++;
    }
    else
    {
        /* Call the worker routine */
        Status = KdpReport(TrapFrame,
                           ExceptionFrame,
                           ExceptionRecord,
                           ContextRecord,
                           PreviousMode,
                           SecondChanceException);
    }

    /* Return TRUE or FALSE to caller */
    return Status;
}

BOOLEAN
NTAPI
KdpStub(IN PKTRAP_FRAME TrapFrame,
        IN PKEXCEPTION_FRAME ExceptionFrame,
        IN PEXCEPTION_RECORD ExceptionRecord,
        IN PCONTEXT ContextRecord,
        IN KPROCESSOR_MODE PreviousMode,
        IN BOOLEAN SecondChanceException)
{
    ULONG ExceptionCommand = ExceptionRecord->ExceptionInformation[0];

    /* Check if this was a breakpoint due to DbgPrint or Load/UnloadSymbols */
    if ((ExceptionRecord->ExceptionCode == STATUS_BREAKPOINT) &&
        (ExceptionRecord->NumberParameters > 0) &&
        ((ExceptionCommand == BREAKPOINT_LOAD_SYMBOLS) ||
         (ExceptionCommand == BREAKPOINT_UNLOAD_SYMBOLS) ||
         (ExceptionCommand == BREAKPOINT_COMMAND_STRING) ||
         (ExceptionCommand == BREAKPOINT_PRINT)))
    {
        /* This we can handle: simply bump EIP */
        ContextRecord->Eip++;
        return TRUE;
    }
    else if (KdPitchDebugger)
    {
        /* There's no debugger, fail. */
        return FALSE;
    }
    else if ((KdAutoEnableOnEvent) &&
             (KdPreviouslyEnabled) &&
             !(KdDebuggerEnabled) &&
             (KdEnableDebugger()) &&
             (KdDebuggerEnabled))
    {
        /* Debugging was Auto-Enabled. We can now send this to KD. */
        return KdpTrap(TrapFrame,
                       ExceptionFrame,
                       ExceptionRecord,
                       ContextRecord,
                       PreviousMode,
                       SecondChanceException);
    }
    else
    {
        /* FIXME: All we can do in this case is trace this exception */
        return FALSE;
    }
}
