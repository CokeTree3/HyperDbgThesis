/**
 * @file DebuggerCommands.c
 * @author Sina Karvandi (sina@hyperdbg.org)
 * @author Alee Amini (alee@hyperdbg.org)
 * @brief Implementation of Debugger Commands
 *
 * @version 0.1
 * @date 2020-04-23
 *
 * @copyright This project is released under the GNU Public License v3.
 *
 */
#include "pch.h"

/**
 * @brief Read memory for different commands
 *
 * @param ReadMemRequest request structure for reading memory
 * @param UserBuffer user buffer to copy the memory
 * @param ReturnSize size that should be returned to user mode buffers
 *
 * @return BOOLEAN
 */
BOOLEAN
DebuggerCommandReadMemory(PDEBUGGER_READ_MEMORY ReadMemRequest, PVOID UserBuffer, PSIZE_T ReturnSize)
{
    UINT32                    Pid;
    UINT32                    Size;
    UINT64                    Address;
    DEBUGGER_READ_MEMORY_TYPE MemType;
    BOOLEAN                   Is32BitProcess = FALSE;

    //
    // Adjust the parameters
    //
    Pid     = ReadMemRequest->Pid;
    Size    = ReadMemRequest->Size;
    Address = ReadMemRequest->Address;
    MemType = ReadMemRequest->MemoryType;

    if (Size && Address != (UINT64)NULL)
    {
        if (MemoryManagerReadProcessMemoryNormal((HANDLE)Pid,
                                                 (PVOID)Address,
                                                 MemType,
                                                 (PVOID)UserBuffer,
                                                 Size,
                                                 ReturnSize))
        {
            //
            // Reading memory was successful
            //

            //
            // *** Now, we check whether this a disassembly request for a virtual address
            // or not, if so, we'll detect whether the target process is 32-bit or 64-bit ***
            //

            //
            // Check if the address is on a 32-bit mode process or not (just in case of disassembling)
            //
            if (ReadMemRequest->MemoryType == DEBUGGER_READ_VIRTUAL_ADDRESS && ReadMemRequest->GetAddressMode)
            {
                //
                // Check if the address is in the canonical range for kernel space
                //
                if (ReadMemRequest->Address >= 0xFFFF800000000000 && ReadMemRequest->Address <= 0xFFFFFFFFFFFFFFFF)
                {
                    //
                    // The address is in the range of canonical kernel space, so it's 64-bit process
                    //
                    ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
                }
                else
                {
                    //
                    // The address is in the user-mode and the memory type is a virtual address
                    // for disassembly, so we have to query whether the target process is a
                    // 32-bit process or a 64-bit process
                    //
                    if (UserAccessIsWow64Process((HANDLE)ReadMemRequest->Pid, &Is32BitProcess))
                    {
                        if (Is32BitProcess)
                        {
                            ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_32_BIT;
                        }
                        else
                        {
                            ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
                        }
                    }
                    else
                    {
                        //
                        // We couldn't determine the type of process, let's assume that it's a
                        // 64-bit process by default
                        //
                        ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
                    }
                }
            }

            //
            // Anyway, the read was successful
            //
            ReadMemRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;
            return TRUE;
        }
        else
        {
            //
            // Reading memory was not successful
            //
            ReadMemRequest->KernelStatus = DEBUGGER_ERROR_READING_MEMORY_INVALID_PARAMETER;
            return FALSE;
        }
    }
    else
    {
        //
        // Parameters are invalid
        //
        ReadMemRequest->KernelStatus = DEBUGGER_ERROR_READING_MEMORY_INVALID_PARAMETER;
        return FALSE;
    }
}

/**
 * @brief Read memory for different commands from vmxroot mode
 *
 * @param ReadMemRequest request structure for reading memory
 * @param UserBuffer user buffer to copy the memory
 * @param ReturnSize size that should be returned to user mode buffers
 * @return BOOLEAN
 */
BOOLEAN
DebuggerCommandReadMemoryVmxRoot(PDEBUGGER_READ_MEMORY ReadMemRequest, UCHAR * UserBuffer, UINT32 * ReturnSize)
{
    UINT32                    Pid;
    UINT32                    Size;
    UINT64                    Address;
    UINT64                    OffsetInUserBuffer;
    DEBUGGER_READ_MEMORY_TYPE MemType;
    BOOLEAN                   Is32BitProcess = FALSE;
    PLIST_ENTRY               TempList       = 0;

    Pid     = ReadMemRequest->Pid;
    Size    = ReadMemRequest->Size;
    Address = ReadMemRequest->Address;
    MemType = ReadMemRequest->MemoryType;

    //
    // read memory safe
    //
    if (MemType == DEBUGGER_READ_PHYSICAL_ADDRESS)
    {
        //
        // Check whether the physical memory is valid or not
        //
        if (!CheckAddressPhysical(Address))
        {
            ReadMemRequest->KernelStatus = DEBUGGER_ERROR_INVALID_PHYSICAL_ADDRESS;
            return FALSE;
        }

        MemoryMapperReadMemorySafeByPhysicalAddress(Address, (UINT64)UserBuffer, Size);
    }
    else if (MemType == DEBUGGER_READ_VIRTUAL_ADDRESS)
    {
        //
        // Check whether the virtual memory is available in the current
        // memory layout and also is present in the RAM
        //
        if (!CheckAccessValidityAndSafety(Address, Size))
        {
            ReadMemRequest->KernelStatus = DEBUGGER_ERROR_INVALID_ADDRESS;
            return FALSE;
        }

        //
        // Read memory safely
        //
        MemoryMapperReadMemorySafeOnTargetProcess(Address, UserBuffer, Size);

        //
        // Check if the target memory is filled with breakpoint of the 'bp' commands
        // if the memory is changed due to this command, then we'll changes it to
        // the previous byte
        //

        //
        // Iterate through the breakpoint list
        //
        TempList = &g_BreakpointsListHead;

        while (&g_BreakpointsListHead != TempList->Flink)
        {
            TempList                                      = TempList->Flink;
            PDEBUGGEE_BP_DESCRIPTOR CurrentBreakpointDesc = CONTAINING_RECORD(TempList, DEBUGGEE_BP_DESCRIPTOR, BreakpointsList);

            if (CurrentBreakpointDesc->Address >= Address && CurrentBreakpointDesc->Address <= Address + Size)
            {
                //
                // The address is found, we have to swap the byte if the target
                // byte is 0xcc
                //

                //
                // Find the address location at user buffer
                //
                OffsetInUserBuffer = CurrentBreakpointDesc->Address - Address;

                if (UserBuffer[OffsetInUserBuffer] == 0xcc)
                {
                    UserBuffer[OffsetInUserBuffer] = CurrentBreakpointDesc->PreviousByte;
                }
            }
        }
    }
    else
    {
        ReadMemRequest->KernelStatus = DEBUGGER_ERROR_MEMORY_TYPE_INVALID;
        return FALSE;
    }

    //
    // Check if the address is on a 32-bit mode process or not (just in case of disassembling)
    //
    if (ReadMemRequest->MemoryType == DEBUGGER_READ_VIRTUAL_ADDRESS && ReadMemRequest->GetAddressMode)
    {
        //
        // Check if the address is in the canonical range for kernel space
        //
        if (ReadMemRequest->Address >= 0xFFFF800000000000 && ReadMemRequest->Address <= 0xFFFFFFFFFFFFFFFF)
        {
            //
            // The address is in the range of canonical kernel space, so it's 64-bit process
            //
            ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
        }
        else
        {
            //
            // The address is in the user-mode and the memory type is a virtual address
            // for disassembly, so we have to query whether the target process is a
            // 32-bit process or a 64-bit process
            //
            if (UserAccessIsWow64ProcessByEprocess(PsGetCurrentProcess(), &Is32BitProcess))
            {
                if (Is32BitProcess)
                {
                    ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_32_BIT;
                }
                else
                {
                    ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
                }
            }
            else
            {
                //
                // We couldn't determine the type of process, let's assume that it's a
                // 64-bit process by default
                //
                ReadMemRequest->AddressMode = DEBUGGER_READ_ADDRESS_MODE_64_BIT;
            }
        }
    }

    //
    // Set the final status of memory read as it was successful
    //
    ReadMemRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;
    *ReturnSize                  = Size;

    return TRUE;
}

/**
 * @brief Perform rdmsr, wrmsr commands
 *
 * @param ReadOrWriteMsrRequest Msr read/write request
 * @param UserBuffer user buffer to save the results
 * @param ReturnSize return size to user-mode buffers
 * @return NTSTATUS
 */
NTSTATUS
DebuggerReadOrWriteMsr(PDEBUGGER_READ_AND_WRITE_ON_MSR ReadOrWriteMsrRequest, UINT64 * UserBuffer, PSIZE_T ReturnSize)
{
    NTSTATUS Status;
    ULONG    ProcessorsCount;

    ProcessorsCount = KeQueryActiveProcessorCount(0);

    //
    // We don't check whether the MSR is in valid range of hardware or not
    // because the user might send a non-valid MSR which means sth to the
    // Windows or VMM, e.g the range specified for VMMs in Hyper-v
    //

    if (ReadOrWriteMsrRequest->ActionType == DEBUGGER_MSR_WRITE)
    {
        //
        // Set Msr to be applied on the target cores
        //
        if (ReadOrWriteMsrRequest->CoreNumber == DEBUGGER_READ_AND_WRITE_ON_MSR_APPLY_ALL_CORES)
        {
            //
            // Means that we should apply it on all cores
            //
            for (size_t i = 0; i < ProcessorsCount; i++)
            {
                g_DbgState[i].MsrState.Msr   = ReadOrWriteMsrRequest->Msr;
                g_DbgState[i].MsrState.Value = ReadOrWriteMsrRequest->Value;
            }
            //
            // Broadcast to all cores to change their Msrs
            //
            KeGenericCallDpc(DpcRoutineWriteMsrToAllCores, 0x0);
        }
        else
        {
            //
            // We have to change a single core's msr
            //

            //
            // Check if the core number is not invalid
            //
            if (ReadOrWriteMsrRequest->CoreNumber >= ProcessorsCount)
            {
                return STATUS_INVALID_PARAMETER;
            }
            //
            // Otherwise it's valid
            //
            g_DbgState[ReadOrWriteMsrRequest->CoreNumber].MsrState.Msr   = ReadOrWriteMsrRequest->Msr;
            g_DbgState[ReadOrWriteMsrRequest->CoreNumber].MsrState.Value = ReadOrWriteMsrRequest->Value;

            //
            // Execute it on a single core
            //
            Status = DpcRoutineRunTaskOnSingleCore(ReadOrWriteMsrRequest->CoreNumber, (PVOID)DpcRoutinePerformWriteMsr, NULL);

            *ReturnSize = 0;
            return Status;
        }

        //
        // It's an wrmsr, nothing to return
        //
        *ReturnSize = 0;
        return STATUS_SUCCESS;
    }
    else if (ReadOrWriteMsrRequest->ActionType == DEBUGGER_MSR_READ)
    {
        //
        // Set Msr to be applied on the target cores
        //
        if (ReadOrWriteMsrRequest->CoreNumber == DEBUGGER_READ_AND_WRITE_ON_MSR_APPLY_ALL_CORES)
        {
            //
            // Means that we should apply it on all cores
            //
            for (size_t i = 0; i < ProcessorsCount; i++)
            {
                g_DbgState[i].MsrState.Msr = ReadOrWriteMsrRequest->Msr;
            }

            //
            // Broadcast to all cores to read their Msrs
            //
            KeGenericCallDpc(DpcRoutineReadMsrToAllCores, 0x0);

            //
            // When we reach here, all processors read their shits
            // so we have to fill that fucking buffer for user mode
            //
            for (size_t i = 0; i < ProcessorsCount; i++)
            {
                UserBuffer[i] = g_DbgState[i].MsrState.Value;
            }

            //
            // It's an rdmsr we have to return a value for all cores
            //

            *ReturnSize = sizeof(UINT64) * ProcessorsCount;
            return STATUS_SUCCESS;
        }
        else
        {
            //
            // Apply to one core
            //

            //
            // Check if the core number is not invalid
            //
            if (ReadOrWriteMsrRequest->CoreNumber >= ProcessorsCount)
            {
                *ReturnSize = 0;
                return STATUS_INVALID_PARAMETER;
            }
            //
            // Otherwise it's valid
            //
            g_DbgState[ReadOrWriteMsrRequest->CoreNumber].MsrState.Msr = ReadOrWriteMsrRequest->Msr;

            //
            // Execute it on a single core
            //
            Status = DpcRoutineRunTaskOnSingleCore(ReadOrWriteMsrRequest->CoreNumber, (PVOID)DpcRoutinePerformReadMsr, NULL);

            if (Status != STATUS_SUCCESS)
            {
                *ReturnSize = 0;
                return Status;
            }
            //
            // Restore the result to the usermode
            //
            UserBuffer[0] = g_DbgState[ReadOrWriteMsrRequest->CoreNumber].MsrState.Value;

            *ReturnSize = sizeof(UINT64);
            return STATUS_SUCCESS;
        }
    }

    *ReturnSize = 0;
    return STATUS_UNSUCCESSFUL;
}

/**
 * @brief Edit physical and virtual memory
 *
 * @param EditMemRequest edit memory request
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandEditMemory(PDEBUGGER_EDIT_MEMORY EditMemRequest)
{
    UINT32 LengthOfEachChunk  = 0;
    PVOID  DestinationAddress = 0;
    PVOID  SourceAddress      = 0;

    //
    // set chunk size in each modification
    //
    if (EditMemRequest->ByteSize == EDIT_BYTE)
    {
        LengthOfEachChunk = 1;
    }
    else if (EditMemRequest->ByteSize == EDIT_DWORD)
    {
        LengthOfEachChunk = 4;
    }
    else if (EditMemRequest->ByteSize == EDIT_QWORD)
    {
        LengthOfEachChunk = 8;
    }
    else
    {
        //
        // Invalid parameter
        //
        EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_PARAMETER;
        return STATUS_UNSUCCESSFUL;
    }

    //
    // Check if address is valid or not valid (virtual address)
    //
    if (EditMemRequest->MemoryType == EDIT_VIRTUAL_MEMORY)
    {
        if (EditMemRequest->ProcessId == HANDLE_TO_UINT32(PsGetCurrentProcessId()) && VirtualAddressToPhysicalAddress((PVOID)EditMemRequest->Address) == 0)
        {
            //
            // It's an invalid address in current process
            //
            EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_ADDRESS_BASED_ON_CURRENT_PROCESS;
            return STATUS_UNSUCCESSFUL;
        }
        else if (VirtualAddressToPhysicalAddressByProcessId((PVOID)EditMemRequest->Address, EditMemRequest->ProcessId) == 0)
        {
            //
            // It's an invalid address in another process
            //
            EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_ADDRESS_BASED_ON_OTHER_PROCESS;
            return STATUS_UNSUCCESSFUL;
        }

        //
        // Edit the memory
        //
        for (size_t i = 0; i < EditMemRequest->CountOf64Chunks; i++)
        {
            DestinationAddress = (PVOID)((UINT64)EditMemRequest->Address + (i * LengthOfEachChunk));
            SourceAddress      = (PVOID)((UINT64)EditMemRequest + SIZEOF_DEBUGGER_EDIT_MEMORY + (i * sizeof(UINT64)));

            //
            // Instead of directly accessing the memory we use the MemoryMapperWriteMemorySafe
            // It is because the target page might be read-only so we can make it writable
            //

            // RtlCopyBytes(DestinationAddress, SourceAddress, LengthOfEachChunk);
            MemoryMapperWriteMemoryUnsafe((UINT64)DestinationAddress, SourceAddress, LengthOfEachChunk, EditMemRequest->ProcessId);
        }
    }
    else if (EditMemRequest->MemoryType == EDIT_PHYSICAL_MEMORY)
    {
        //
        // Check whether the physical addres
        //
        if (!CheckAddressPhysical(EditMemRequest->Address))
        {
            EditMemRequest->Result = DEBUGGER_ERROR_INVALID_ADDRESS;
            return STATUS_UNSUCCESSFUL;
        }

        //
        // Edit the physical memory
        //
        for (size_t i = 0; i < EditMemRequest->CountOf64Chunks; i++)
        {
            DestinationAddress = (PVOID)((UINT64)EditMemRequest->Address + (i * LengthOfEachChunk));
            SourceAddress      = (PVOID)((UINT64)EditMemRequest + SIZEOF_DEBUGGER_EDIT_MEMORY + (i * sizeof(UINT64)));

            // MemoryMapperWriteMemorySafeByPhysicalAddress((UINT64)DestinationAddress, (UINT64)SourceAddress, LengthOfEachChunk);
            // WritePhysicalMemoryUsingMapIoSpace((PVOID)SourceAddress, (PVOID)DestinationAddress, LengthOfEachChunk);

            if (MemoryManagerWritePhysicalMemoryNormal((PVOID)DestinationAddress, (PVOID)SourceAddress, (SIZE_T)LengthOfEachChunk) == FALSE)
            {
                EditMemRequest->Result = DEBUGGER_ERROR_INVALID_ADDRESS;
                return STATUS_UNSUCCESSFUL;
            }
        }
    }
    else
    {
        //
        // Invalid parameter
        //
        EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_PARAMETER;
        return STATUS_UNSUCCESSFUL;
    }

    //
    // Set the resutls
    //
    EditMemRequest->Result = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Edit physical and virtual memory on vmxroot mode
 *
 * @param EditMemRequest edit memory request
 * @return NTSTATUS
 */
BOOLEAN
DebuggerCommandEditMemoryVmxRoot(PDEBUGGER_EDIT_MEMORY EditMemRequest)
{
    UINT32 LengthOfEachChunk  = 0;
    PVOID  DestinationAddress = 0;
    PVOID  SourceAddress      = 0;

    //
    // THIS FUNCTION IS SAFE TO BE CALLED FROM VMX-ROOT
    //

    //
    // set chunk size in each modification
    //
    if (EditMemRequest->ByteSize == EDIT_BYTE)
    {
        LengthOfEachChunk = 1;
    }
    else if (EditMemRequest->ByteSize == EDIT_DWORD)
    {
        LengthOfEachChunk = 4;
    }
    else if (EditMemRequest->ByteSize == EDIT_QWORD)
    {
        LengthOfEachChunk = 8;
    }
    else
    {
        //
        // Invalid parameter
        //
        EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_PARAMETER;
        return FALSE;
    }

    if (EditMemRequest->MemoryType == EDIT_VIRTUAL_MEMORY)
    {
        //
        // Check whether the virtual memory is available in the current
        // memory layout and also is present in the RAM
        //
        if (!CheckAccessValidityAndSafety(EditMemRequest->Address,
                                          EditMemRequest->ByteSize * EditMemRequest->CountOf64Chunks))
        {
            EditMemRequest->Result = DEBUGGER_ERROR_INVALID_ADDRESS;
            return FALSE;
        }

        //
        // Edit the memory
        //
        for (size_t i = 0; i < EditMemRequest->CountOf64Chunks; i++)
        {
            DestinationAddress = (PVOID)((UINT64)EditMemRequest->Address + (i * LengthOfEachChunk));
            SourceAddress      = (PVOID)((UINT64)EditMemRequest + SIZEOF_DEBUGGER_EDIT_MEMORY + (i * sizeof(UINT64)));

            //
            // Instead of directly accessing the memory we use the MemoryMapperWriteMemorySafeOnTargetProcess
            // It is because the target page might be read-only so we can make it writable
            //

            // RtlCopyBytes(DestinationAddress, SourceAddress, LengthOfEachChunk);
            MemoryMapperWriteMemorySafeOnTargetProcess((UINT64)DestinationAddress, SourceAddress, LengthOfEachChunk);
        }
    }
    else if (EditMemRequest->MemoryType == EDIT_PHYSICAL_MEMORY)
    {
        //
        // Check whether the physical addres
        //
        if (!CheckAddressPhysical(EditMemRequest->Address))
        {
            EditMemRequest->Result = DEBUGGER_ERROR_INVALID_ADDRESS;
            return FALSE;
        }

        //
        // Edit the physical memory
        //
        for (size_t i = 0; i < EditMemRequest->CountOf64Chunks; i++)
        {
            DestinationAddress = (PVOID)((UINT64)EditMemRequest->Address + (i * LengthOfEachChunk));
            SourceAddress      = (PVOID)((UINT64)EditMemRequest + SIZEOF_DEBUGGER_EDIT_MEMORY + (i * sizeof(UINT64)));

            MemoryMapperWriteMemorySafeByPhysicalAddress((UINT64)DestinationAddress, (UINT64)SourceAddress, LengthOfEachChunk);
        }
    }
    else
    {
        //
        // Invalid parameter
        //
        EditMemRequest->Result = DEBUGGER_ERROR_EDIT_MEMORY_STATUS_INVALID_PARAMETER;
        return FALSE;
    }

    //
    // Set the resutls
    //
    EditMemRequest->Result = DEBUGGER_OPERATION_WAS_SUCCESSFUL;
    return TRUE;
}

/**
 * @brief Search on virtual memory (not work on physical memory)
 *
 * @details This function can be called from vmx-root mode
 * Do NOT directly call this function as the virtual addresses
 * should be valid on the target process memory layout
 * instead call : SearchAddressWrapper
 * the address between StartAddress and EndAddress should be contiguous
 *
 * @param AddressToSaveResults Address to save the search results
 * @param SearchMemRequest request structure of searching memory
 * @param StartAddress valid start address based on target process
 * @param EndAddress valid end address based on target process
 * @param IsDebuggeePaused Set to true when the search is performed in
 * the debugger mode
 * @param CountOfMatchedCases Number of matched cases
 * @return BOOLEAN Whether the search was successful or not
 */
BOOLEAN
PerformSearchAddress(UINT64 *                AddressToSaveResults,
                     PDEBUGGER_SEARCH_MEMORY SearchMemRequest,
                     UINT64                  StartAddress,
                     UINT64                  EndAddress,
                     BOOLEAN                 IsDebuggeePaused,
                     PUINT32                 CountOfMatchedCases)
{
    UINT32   CountOfOccurance      = 0;
    UINT64   Cmp64                 = 0;
    UINT32   IndexToArrayOfResults = 0;
    UINT32   LengthOfEachChunk     = 0;
    PVOID    TempSourceAddress     = 0;
    PVOID    SourceAddress         = 0;
    BOOLEAN  StillMatch            = FALSE;
    UINT64   TempValue             = (UINT64)NULL;
    CR3_TYPE CurrentProcessCr3     = {0};

    //
    // set chunk size in each modification
    //
    if (SearchMemRequest->ByteSize == SEARCH_BYTE)
    {
        LengthOfEachChunk = 1;
    }
    else if (SearchMemRequest->ByteSize == SEARCH_DWORD)
    {
        LengthOfEachChunk = 4;
    }
    else if (SearchMemRequest->ByteSize == SEARCH_QWORD)
    {
        LengthOfEachChunk = 8;
    }
    else
    {
        //
        // Invalid parameter
        //
        return FALSE;
    }

    //
    // Check if address is virtual address or physical address
    //
    if (SearchMemRequest->MemoryType == SEARCH_VIRTUAL_MEMORY ||
        SearchMemRequest->MemoryType == SEARCH_PHYSICAL_FROM_VIRTUAL_MEMORY)
    {
        //
        // Search the memory
        //

        //
        // Change the memory layout (cr3), if the user specified a
        // special process
        //
        if (IsDebuggeePaused)
        {
            //
            // Switch to target process memory layout
            //
            CurrentProcessCr3 = SwitchToProcessMemoryLayoutByCr3(LayoutGetCurrentProcessCr3());
        }
        else
        {
            if (SearchMemRequest->ProcessId != HANDLE_TO_UINT32(PsGetCurrentProcessId()))
            {
                CurrentProcessCr3 = SwitchToProcessMemoryLayout(SearchMemRequest->ProcessId);
            }
        }

        //
        // Here we iterate through the buffer we received from
        // user-mode
        //
        SourceAddress = (PVOID)((UINT64)SearchMemRequest + SIZEOF_DEBUGGER_SEARCH_MEMORY);

        for (size_t BaseIterator = (size_t)StartAddress; BaseIterator < ((UINT64)EndAddress); BaseIterator += LengthOfEachChunk)
        {
            //
            // *** Search the memory ***
            //

            //
            // Copy 64bit, 32bit or one byte value into Cmp64 buffer and then compare it
            // Check if we should access the memory directly, or through safe memory
            // routine from vmx-root
            //
            if (IsDebuggeePaused)
            {
                MemoryMapperReadMemorySafe((UINT64)BaseIterator, &Cmp64, LengthOfEachChunk);
            }
            else
            {
                RtlCopyMemory(&Cmp64, (PVOID)BaseIterator, LengthOfEachChunk);
            }

            //
            // Get the searching bytes
            //
            TempValue = *(UINT64 *)SourceAddress;

            //
            // Check whether the byte matches the source or not
            //
            if (Cmp64 == TempValue)
            {
                //
                // Indicate that it matches until now
                //
                StillMatch = TRUE;

                //
                // Try to check each element (we don't start from the very first element as
                // it checked before )
                //
                for (size_t i = LengthOfEachChunk; i < SearchMemRequest->CountOf64Chunks; i++)
                {
                    //
                    // I know, we have a double check here ;)
                    //
                    TempSourceAddress = (PVOID)((UINT64)SearchMemRequest + SIZEOF_DEBUGGER_SEARCH_MEMORY + (i * sizeof(UINT64)));

                    //
                    // Add i to BaseIterator and recompute the Cmp64
                    // Check if we should access the memory directly, or through safe memory
                    // routine from vmx-root
                    //
                    if (IsDebuggeePaused)
                    {
                        MemoryMapperReadMemorySafe((UINT64)(BaseIterator + (LengthOfEachChunk * i)), &Cmp64, LengthOfEachChunk);
                    }
                    else
                    {
                        RtlCopyMemory(&Cmp64, (PVOID)(BaseIterator + (LengthOfEachChunk * i)), LengthOfEachChunk);
                    }

                    //
                    // Check if we should access the memory directly,
                    // or through safe memory routine from vmx-root
                    //
                    if (IsDebuggeePaused)
                    {
                        MemoryMapperReadMemorySafe((UINT64)TempSourceAddress, &TempValue, sizeof(UINT64));
                    }
                    else
                    {
                        TempValue = *(UINT64 *)TempSourceAddress;
                    }

                    if (!(Cmp64 == TempValue))
                    {
                        //
                        // One thing didn't match so this is not the pattern
                        //
                        StillMatch = FALSE;

                        //
                        // Break from the loop
                        //
                        break;
                    }
                }

                //
                // Check if we find the pattern or not
                //
                if (StillMatch)
                {
                    //
                    // We found the a matching address, let's save the
                    // address for future use
                    //
                    CountOfOccurance++;

                    if (IsDebuggeePaused)
                    {
                        if (SearchMemRequest->MemoryType == SEARCH_PHYSICAL_FROM_VIRTUAL_MEMORY)
                        {
                            //
                            // It's a physical memory
                            //
                            Log("%llx\n", VirtualAddressToPhysicalAddress((PVOID)BaseIterator));
                        }
                        else
                        {
                            //
                            // It's a virtual memory
                            //
                            Log("%llx\n", BaseIterator);
                        }
                    }
                    else
                    {
                        if (SearchMemRequest->MemoryType == SEARCH_PHYSICAL_FROM_VIRTUAL_MEMORY)
                        {
                            //
                            // It's a physical memory
                            //
                            AddressToSaveResults[IndexToArrayOfResults] = VirtualAddressToPhysicalAddress((PVOID)BaseIterator);
                        }
                        else
                        {
                            //
                            // It's a virtual memory
                            //
                            AddressToSaveResults[IndexToArrayOfResults] = BaseIterator;
                        }
                    }

                    //
                    // Increase the array pointer if it doesn't exceed the limitation
                    //
                    if (MaximumSearchResults > IndexToArrayOfResults)
                    {
                        IndexToArrayOfResults++;
                    }
                    else
                    {
                        //
                        // The result buffer is full!
                        //
                        *CountOfMatchedCases = CountOfOccurance;
                        return TRUE;
                    }
                }
            }
            else
            {
                //
                // Not found in the place
                //
                continue;
            }
        }

        //
        // Restore the previous memory layout (cr3), if the user specified a
        // special process
        //
        if (IsDebuggeePaused || SearchMemRequest->ProcessId != HANDLE_TO_UINT32(PsGetCurrentProcessId()))
        {
            SwitchToPreviousProcess(CurrentProcessCr3);
        }
    }
    else if (SearchMemRequest->MemoryType == SEARCH_PHYSICAL_MEMORY)
    {
        //
        // That's an error, the physical memory is handled like virtual memory and
        // thus we should never reach here
        //
        LogError("Err, searching physical memory is not allowed without virtual address");

        return FALSE;
    }
    else
    {
        //
        // Invalid parameter
        //
        return FALSE;
    }

    //
    // As we're here the search is finished without error
    //
    *CountOfMatchedCases = CountOfOccurance;
    return TRUE;
}

/**
 * @brief The wrapper to check for validity of addresses and call
 * the search routines for both physical and virtual memory
 *
 * @details This function can be called from vmx-root mode
 * The address between start address and end address will be checked
 * to make a contiguous address
 *
 * @param AddressToSaveResults Address to save the search results
 * @param SearchMemRequest request structure of searching memory
 * @param StartAddress start address of searching based on target process
 * @param EndAddress start address of searching based on target process
 * @param IsDebuggeePaused Set to true when the search is performed in
 * the debugger mode
 * @param CountOfMatchedCases Number of matched cases
 * @return BOOLEAN Whether there was any error or not
 */
BOOLEAN
SearchAddressWrapper(PUINT64                 AddressToSaveResults,
                     PDEBUGGER_SEARCH_MEMORY SearchMemRequest,
                     UINT64                  StartAddress,
                     UINT64                  EndAddress,
                     BOOLEAN                 IsDebuggeePaused,
                     PUINT32                 CountOfMatchedCases)
{
    CR3_TYPE CurrentProcessCr3;
    UINT64   BaseAddress         = 0;
    UINT64   RealPhysicalAddress = 0;
    UINT64   TempValue           = (UINT64)NULL;
    UINT64   TempStartAddress    = (UINT64)NULL;
    BOOLEAN  DoesBaseAddrSaved   = FALSE;
    BOOLEAN  SearchResult        = FALSE;

    //
    // Reset the count of matched cases
    //
    *CountOfMatchedCases = 0;

    if (SearchMemRequest->MemoryType == SEARCH_VIRTUAL_MEMORY)
    {
        //
        // It's a virtual address search
        //

        //
        // Align the page and search with alignment
        //
        TempStartAddress = StartAddress;
        StartAddress     = (UINT64)PAGE_ALIGN(StartAddress);

        if (IsDebuggeePaused)
        {
            //
            // Switch to new process's memory layout
            //
            CurrentProcessCr3 = SwitchToProcessMemoryLayoutByCr3(LayoutGetCurrentProcessCr3());
        }
        else
        {
            //
            // Switch to new process's memory layout
            //
            CurrentProcessCr3 = SwitchToProcessMemoryLayout(SearchMemRequest->ProcessId);
        }

        //
        // We will try to find a contigues address
        //
        while (StartAddress < EndAddress)
        {
            //
            // Check if address is valid or not
            // Generally, we can use VirtualAddressToPhysicalAddressByProcessId
            // but let's not change the cr3 multiple times
            //
            TempValue = VirtualAddressToPhysicalAddress((PVOID)StartAddress);

            if (TempValue != 0)
            {
                //
                // Address is valid, let's add a page size to it
                // nothing to do
                //
                if (!DoesBaseAddrSaved)
                {
                    BaseAddress       = TempStartAddress;
                    DoesBaseAddrSaved = TRUE;
                }
            }
            else
            {
                //
                // Address is not valid anymore
                //
                break;
            }

            //
            // Make the start address ready for next address
            //
            StartAddress += PAGE_SIZE;
        }

        //
        // Restore the original process
        //
        SwitchToPreviousProcess(CurrentProcessCr3);

        //
        // All of the address chunk was valid
        //
        if (DoesBaseAddrSaved && StartAddress > BaseAddress)
        {
            SearchResult = PerformSearchAddress(AddressToSaveResults,
                                                SearchMemRequest,
                                                BaseAddress,
                                                EndAddress,
                                                IsDebuggeePaused,
                                                CountOfMatchedCases);
        }
        else
        {
            //
            // There was an error, address was probably not contiguous
            //
            return FALSE;
        }
    }
    else if (SearchMemRequest->MemoryType == SEARCH_PHYSICAL_MEMORY)
    {
        //
        // when we reached here, we know that it's a valid physical memory,
        // so we change the structure and pass it as a virtual address to
        // the search function
        //
        RealPhysicalAddress = SearchMemRequest->Address;

        //
        // Change the start address
        //
        if (IsDebuggeePaused)
        {
            SearchMemRequest->Address = PhysicalAddressToVirtualAddressOnTargetProcess((PVOID)StartAddress);
            EndAddress                = PhysicalAddressToVirtualAddressOnTargetProcess((PVOID)EndAddress);
        }
        else if (SearchMemRequest->ProcessId == HANDLE_TO_UINT32(PsGetCurrentProcessId()))
        {
            SearchMemRequest->Address = PhysicalAddressToVirtualAddress(StartAddress);
            EndAddress                = PhysicalAddressToVirtualAddress(EndAddress);
        }
        else
        {
            SearchMemRequest->Address = PhysicalAddressToVirtualAddressByProcessId((PVOID)StartAddress,
                                                                                   SearchMemRequest->ProcessId);
            EndAddress                = PhysicalAddressToVirtualAddressByProcessId((PVOID)EndAddress,
                                                                    SearchMemRequest->ProcessId);
        }

        //
        // Change the type of memory
        //
        SearchMemRequest->MemoryType = SEARCH_PHYSICAL_FROM_VIRTUAL_MEMORY;

        //
        // Call the wrapper
        //
        SearchResult = PerformSearchAddress(AddressToSaveResults,
                                            SearchMemRequest,
                                            SearchMemRequest->Address,
                                            EndAddress,
                                            IsDebuggeePaused,
                                            CountOfMatchedCases);

        //
        // Restore the previous state
        //
        SearchMemRequest->MemoryType = SEARCH_PHYSICAL_MEMORY;
        SearchMemRequest->Address    = RealPhysicalAddress;
    }

    return SearchResult;
}

/**
 * @brief Start searching memory
 *
 * @param SearchMemRequest Request to search memory
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandSearchMemory(PDEBUGGER_SEARCH_MEMORY SearchMemRequest)
{
    PUINT64 SearchResultsStorage = NULL;
    PUINT64 UsermodeBuffer       = NULL;
    UINT64  AddressFrom          = 0;
    UINT64  AddressTo            = 0;
    UINT64  CurrentValue         = 0;
    UINT32  ResultsIndex         = 0;
    UINT32  CountOfResults       = 0;

    //
    // Check if process id is valid or not
    //
    if (SearchMemRequest->ProcessId != HANDLE_TO_UINT32(PsGetCurrentProcessId()) && !CommonIsProcessExist(SearchMemRequest->ProcessId))
    {
        return STATUS_INVALID_PARAMETER;
    }

    //
    // User-mode buffer is same as SearchMemRequest
    //
    UsermodeBuffer = (UINT64 *)SearchMemRequest;

    //
    // We store the user-mode data in a separate variable because
    // we will use them later when we Zeroed the SearchMemRequest
    //
    AddressFrom = SearchMemRequest->Address;
    AddressTo   = SearchMemRequest->Address + SearchMemRequest->Length;

    //
    // We support up to MaximumSearchResults search results
    //
    SearchResultsStorage = PlatformMemAllocateZeroedNonPagedPool(MaximumSearchResults * sizeof(UINT64));

    if (SearchResultsStorage == NULL)
    {
        //
        // Not enough memory
        //
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    //
    // Call the wrapper
    //
    SearchAddressWrapper(SearchResultsStorage, SearchMemRequest, AddressFrom, AddressTo, FALSE, &CountOfResults);

    //
    // In this point, we to store the results (if any) to the user-mode
    // buffer SearchMemRequest itself is the user-mode buffer and we also
    // checked from the previous function that the output buffer is at
    // least SearchMemRequest bigger or equal to MaximumSearchResults * sizeof(UINT64)
    // so we need to clear everything here, and also we should keep in mind that
    // SearchMemRequest is no longer valid
    //
    RtlZeroMemory(SearchMemRequest, MaximumSearchResults * sizeof(UINT64));

    //
    // It's time to move the results from our temporary buffer to the user-mode
    // buffer, also there is something that we should check and that's the fact
    // that we used aligned page addresses so the results should be checked to
    // see whether the results are between the user's entered addresses or not
    //
    for (size_t i = 0; i < MaximumSearchResults; i++)
    {
        CurrentValue = SearchResultsStorage[i];

        if (CurrentValue == (UINT64)NULL)
        {
            //
            // Nothing left to move
            //
            break;
        }

        if (CurrentValue >= AddressFrom && CurrentValue <= AddressTo)
        {
            //
            // Move the variable
            //
            UsermodeBuffer[ResultsIndex] = CurrentValue;
            ResultsIndex++;
        }
    }

    //
    // Free the results pool
    //
    PlatformMemFreePool(SearchResultsStorage);

    return STATUS_SUCCESS;
}

/**
 * @brief Perform the flush requests to vmx-root and vmx non-root buffers
 *
 * @param DebuggerFlushBuffersRequest Request to flush the buffers
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandFlush(PDEBUGGER_FLUSH_LOGGING_BUFFERS DebuggerFlushBuffersRequest)
{
    //
    // We try to flush buffers for both vmx-root and regular kernel buffer
    //
    DebuggerFlushBuffersRequest->CountOfMessagesThatSetAsReadFromVmxRoot    = LogMarkAllAsRead(TRUE);
    DebuggerFlushBuffersRequest->CountOfMessagesThatSetAsReadFromVmxNonRoot = LogMarkAllAsRead(FALSE);
    DebuggerFlushBuffersRequest->KernelStatus                               = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Perform the command finished signal
 *
 * @param DebuggerFinishedExecutionRequest Request to
 * signal debuggee about execution state
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandSignalExecutionState(PDEBUGGER_SEND_COMMAND_EXECUTION_FINISHED_SIGNAL DebuggerFinishedExecutionRequest)
{
    //
    // It's better to send the signal from vmx-root mode
    //
    VmFuncVmxVmcall(DEBUGGER_VMCALL_SIGNAL_DEBUGGER_EXECUTION_FINISHED, 0, 0, 0);

    DebuggerFinishedExecutionRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Send the user-mode buffer to debugger
 *
 * @param DebuggerSendUsermodeMessageRequest Request to send message to debugger
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandSendMessage(PDEBUGGER_SEND_USERMODE_MESSAGES_TO_DEBUGGER DebuggerSendUsermodeMessageRequest)
{
    //
    // It's better to send the signal from vmx-root mode to avoid deadlock
    //
    VmFuncVmxVmcall(DEBUGGER_VMCALL_SEND_MESSAGES_TO_DEBUGGER,
                    (UINT64)DebuggerSendUsermodeMessageRequest + (SIZEOF_DEBUGGER_SEND_USERMODE_MESSAGES_TO_DEBUGGER),
                    DebuggerSendUsermodeMessageRequest->Length,
                    0);

    DebuggerSendUsermodeMessageRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Send general buffers from the debuggee to the debugger
 *
 * @param DebuggeeBufferRequest Request to buffer that will be sent to the debugger
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandSendGeneralBufferToDebugger(PDEBUGGEE_SEND_GENERAL_PACKET_FROM_DEBUGGEE_TO_DEBUGGER DebuggeeBufferRequest)
{
    //
    // It's better to send the signal from vmx-root mode to avoid deadlock
    //
    VmFuncVmxVmcall(DEBUGGER_VMCALL_SEND_GENERAL_BUFFER_TO_DEBUGGER,
                    (UINT64)DebuggeeBufferRequest,
                    0,
                    0);

    DebuggeeBufferRequest->KernelResult = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Reserve and allocate pre-allocated buffers
 *
 * @param PreallocRequest Request details of needed buffers to be reserved
 *
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandReservePreallocatedPools(PDEBUGGER_PREALLOC_COMMAND PreallocRequest)
{
    switch (PreallocRequest->Type)
    {
    case DEBUGGER_PREALLOC_COMMAND_TYPE_THREAD_INTERCEPTION:

        //
        // Request pages to be allocated for thread interception mechanism
        //
        PoolManagerRequestAllocation(sizeof(USERMODE_DEBUGGING_THREAD_HOLDER),
                                     PreallocRequest->Count,
                                     PROCESS_THREAD_HOLDER);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_MONITOR:

        //
        // Perform the allocations for the '!monitor' command
        //
        ConfigureEptHookAllocateExtraHookingPagesForMemoryMonitorsAndExecEptHooks(PreallocRequest->Count);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_EPTHOOK:

        //
        // Perform the allocations for the '!epthook' command
        //
        ConfigureEptHookAllocateExtraHookingPagesForMemoryMonitorsAndExecEptHooks(PreallocRequest->Count);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_EPTHOOK2:

        //
        // All the prealloc requests of regular EPT hooks are needed for the '!epthook2'
        //
        ConfigureEptHookReservePreallocatedPoolsForEptHooks(PreallocRequest->Count);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_REGULAR_EVENT:

        //
        // Request pages to be allocated for regular instant events
        //
        PoolManagerRequestAllocation(REGULAR_INSTANT_EVENT_CONDITIONAL_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_REGULAR_EVENT_BUFFER);

        //
        // Request pages to be allocated for regular instant events's actions
        //
        PoolManagerRequestAllocation(REGULAR_INSTANT_EVENT_ACTION_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_REGULAR_EVENT_ACTION_BUFFER);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_BIG_EVENT:

        //
        // Request pages to be allocated for big instant events
        //
        PoolManagerRequestAllocation(BIG_INSTANT_EVENT_CONDITIONAL_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_BIG_EVENT_BUFFER);

        //
        // Request pages to be allocated for big instant events's actions
        //
        PoolManagerRequestAllocation(BIG_INSTANT_EVENT_ACTION_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_BIG_EVENT_ACTION_BUFFER);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_REGULAR_SAFE_BUFFER:

        //
        // Request pages to be allocated for regular safe buffer ($buffer) for events
        //
        PoolManagerRequestAllocation(REGULAR_INSTANT_EVENT_REQUESTED_SAFE_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_REGULAR_SAFE_BUFFER_FOR_EVENTS);

        break;

    case DEBUGGER_PREALLOC_COMMAND_TYPE_BIG_SAFE_BUFFER:

        //
        // Request pages to be allocated for big safe buffer ($buffer) for events
        //
        PoolManagerRequestAllocation(BIG_INSTANT_EVENT_REQUESTED_SAFE_BUFFER,
                                     PreallocRequest->Count,
                                     INSTANT_BIG_SAFE_BUFFER_FOR_EVENTS);

        break;

    default:

        PreallocRequest->KernelStatus = DEBUGGER_ERROR_COULD_NOT_FIND_ALLOCATION_TYPE;
        return STATUS_UNSUCCESSFUL;
    }

    //
    // Invalidate and perform the allocations as we're in PASSIVE_LEVEL
    //
    PoolManagerCheckAndPerformAllocationAndDeallocation();

    PreallocRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief Preactivate a special functionality
 *
 * @param PreactivateRequest Request details of preactivation
 *
 * @return NTSTATUS
 */
NTSTATUS
DebuggerCommandPreactivateFunctionality(PDEBUGGER_PREACTIVATE_COMMAND PreactivateRequest)
{
    switch (PreactivateRequest->Type)
    {
    case DEBUGGER_PREACTIVATE_COMMAND_TYPE_MODE:

        //
        // Request for allocating the mode mechanism
        //
        ConfigureInitializeExecTrapOnAllProcessors();

        break;

    default:

        PreactivateRequest->KernelStatus = DEBUGGER_ERROR_COULD_NOT_FIND_PREACTIVATION_TYPE;
        return STATUS_UNSUCCESSFUL;
    }

    PreactivateRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return STATUS_SUCCESS;
}

/**
 * @brief routines for the .pagein command
 *
 * @param PageinRequest
 *
 * @return BOOLEAN
 */
BOOLEAN
DebuggerCommandBringPagein(PDEBUGGER_PAGE_IN_REQUEST PageinRequest)
{
    //
    // *** Perform the injection here ***
    //
    LogInfo("Page-request is received!");

    //
    // Adjust the flags for showing the successful #PF injection
    //
    PageinRequest->KernelStatus = DEBUGGER_OPERATION_WAS_SUCCESSFUL;

    return TRUE;
}
