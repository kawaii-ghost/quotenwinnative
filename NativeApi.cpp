#include <Veil.h>

static USER_THREAD_START_ROUTINE KeyboardWatcher;

#ifndef KEY_BREAK
#define KEY_BREAK 0x01
#endif

typedef struct _KEYBOARD_INPUT_DATA {
    USHORT UnitId;
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    ULONG ExtraInformation;
} KEYBOARD_INPUT_DATA, * PKEYBOARD_INPUT_DATA;

typedef struct _QUOTE_SET {
    HANDLE SectionHandle;
    PVOID ViewBase;
    PWSTR* Entries;
    ULONG Count;
} QUOTE_SET, * PQUOTE_SET;

#define LINE_WIDTH 80

static NTSTATUS MapQuotesFile(PCWSTR NtPath, PVOID* OutView, PULONG OutCharCount, PHANDLE OutSection) {
    UNICODE_STRING PathU;
    RtlInitUnicodeString(&PathU, NtPath);

    OBJECT_ATTRIBUTES FileObj;
    InitializeObjectAttributes(&FileObj,
                               &PathU,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE FileHandle;

    NTSTATUS Status = NtCreateFile(&FileHandle,
                                   FILE_READ_DATA | SYNCHRONIZE,
                                   &FileObj, &IoStatusBlock,
                                   NULL,
                                   FILE_ATTRIBUTE_NORMAL,
                                   FILE_SHARE_READ,
                                   FILE_OPEN,
                                   FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE,
                                   NULL,
                                   0);
    if (!NT_SUCCESS(Status))
        return Status;

    HANDLE SectionHandle;
    Status = NtCreateSection(&SectionHandle,
                             STANDARD_RIGHTS_REQUIRED | SECTION_MAP_READ | SECTION_QUERY,
                             NULL,
                             NULL,
                             PAGE_WRITECOPY,
                             SEC_COMMIT,
                             FileHandle);
    NtClose(FileHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    PVOID ViewBase = NULL;
    SIZE_T ViewSize = 0;
    Status = NtMapViewOfSection(SectionHandle,
                                NtCurrentProcess(),
                                &ViewBase,
                                0,
                                0,
                                NULL,
                                &ViewSize,
                                ViewUnmap,
                                0,
                                PAGE_WRITECOPY);
    if (!NT_SUCCESS(Status)) {
        NtClose(SectionHandle);
        return Status;
    }

    *OutView = ViewBase;
    *OutCharCount = static_cast<ULONG>(ViewSize / sizeof(WCHAR));
    *OutSection = SectionHandle;
    return STATUS_SUCCESS;
}

static BOOLEAN IsDelimiterLine(PCWSTR Start, ULONG Len) {
    return Len == 3 && Start[0] == L'-' && Start[1] == L'-' && Start[2] == L'-';
}

static NTSTATUS ParseQuotes(PWSTR View, ULONG CharCount, PQUOTE_SET Set) {
    ULONG i = 0;
    if (CharCount > 0 && View[0] == 0xFEFF)
        i = 1;

    ULONG Count = 0;
    BOOLEAN HasContent = FALSE;

    for (ULONG p = i; p <= CharCount; p++) {
        ULONG LineStart = p;
        while (p < CharCount && View[p] != L'\n')
            p++;
        ULONG LineEnd = p;
        if (LineEnd > LineStart && View[LineEnd - 1] == L'\r')
            LineEnd--;
        ULONG LineLen = LineEnd - LineStart;

        if (IsDelimiterLine(&View[LineStart], LineLen)) {
            if (HasContent) {
                Count++;
                HasContent = FALSE;
            }
        } else if (LineLen > 0 && View[LineStart] != L'#') {
            HasContent = TRUE;
        }
    }
    if (HasContent)
        Count++;
    if (Count == 0)
        return STATUS_NOT_FOUND;

    Set->Entries = reinterpret_cast<PWSTR*>(RtlAllocateHeap(_VEIL_IMPL_RtlProcessHeap(), HEAP_NO_SERIALIZE, Count * sizeof(PWSTR)));
    if (!Set->Entries)
        return STATUS_NO_MEMORY;

    ULONG w = i;
    ULONG EntryStart = i;
    ULONG EntryIdx = 0;
    BOOLEAN AnyLineInEntry = FALSE;

    for (ULONG p = i; p <= CharCount; p++) {
        ULONG LineStart = p;
        while (p < CharCount && View[p] != L'\n')
            p++;
        ULONG LineEnd = p;
        if (LineEnd > LineStart && View[LineEnd - 1] == L'\r')
            LineEnd--;
        ULONG LineLen = LineEnd - LineStart;
        BOOLEAN IsLast = (p >= CharCount);

        if (IsDelimiterLine(&View[LineStart], LineLen)) {
            if (AnyLineInEntry) {
                if (w > EntryStart && View[w - 1] == L'\n')
                    w--;
                View[w] = L'\0';
                Set->Entries[EntryIdx++] = &View[EntryStart];
                w++;
                EntryStart = w;
                AnyLineInEntry = FALSE;
            }
        } else if (LineLen > 0 && View[LineStart] == L'#') {
            // Skip comment
        } else {
            for (ULONG k = LineStart; k < LineEnd; k++)
                View[w++] = View[k];
            if (!IsLast)
                View[w++] = L'\n';
            if (LineLen > 0)
                AnyLineInEntry = TRUE;
        }
    }
    if (AnyLineInEntry) {
        if (w > EntryStart && View[w - 1] == L'\n')
            w--;
        View[w] = L'\0';
        Set->Entries[EntryIdx++] = &View[EntryStart];
    }

    Set->Count = EntryIdx;
    return STATUS_SUCCESS;
}

static VOID FreeQuoteSet(PQUOTE_SET Set) {
    if (Set->Entries)
        RtlFreeHeap(_VEIL_IMPL_RtlProcessHeap(), HEAP_NO_SERIALIZE, Set->Entries);
    if (Set->ViewBase)
        NtUnmapViewOfSection(NtCurrentProcess(), Set->ViewBase);
    if (Set->SectionHandle)
        NtClose(Set->SectionHandle);
}

static NTSTATUS NTAPI KeyboardWatcher(PVOID Arg) {
    UNREFERENCED_PARAMETER(Arg);

    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"\\Device\\KeyboardClass0");

    OBJECT_ATTRIBUTES Obj;
    InitializeObjectAttributes(&Obj, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);

    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE KbdHandle;

    NTSTATUS Status = NtCreateFile(&KbdHandle,
                                   GENERIC_READ | SYNCHRONIZE,
                                   &Obj,
                                   &IoStatusBlock,
                                   NULL,
                                   FILE_ATTRIBUTE_NORMAL,
                                   0,
                                   FILE_OPEN,
                                   FILE_DIRECTORY_FILE,
                                   NULL,
                                   0);
    if (!NT_SUCCESS(Status))
        return Status;

    HANDLE KbdEvent;
    Status = NtCreateEvent(&KbdEvent,
                           EVENT_ALL_ACCESS,
                           NULL,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status)) {
        NtClose(KbdHandle);
        return Status;
    }

    KEYBOARD_INPUT_DATA Data;

    for (;;) {
        LARGE_INTEGER ByteOffset = { 0 };
        Status = NtReadFile(KbdHandle,
                            KbdEvent,
                            NULL,
                            NULL,
                            &IoStatusBlock,
                            &Data,
                            sizeof(Data),
                            &ByteOffset,
                            NULL);

        if (Status == STATUS_PENDING) {
            NtWaitForSingleObject(KbdEvent, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        if (!NT_SUCCESS(Status))
            break;
        if (Data.Flags & KEY_BREAK)
            continue; 
        break;
    }

    NtClose(KbdEvent);
    NtClose(KbdHandle);

    if (NT_SUCCESS(Status)) {
        RtlExitUserProcess(STATUS_SUCCESS);
    }

    return Status;
}

// ---------------------------------------------------------------------------
// RNG & typewriter effect
// ---------------------------------------------------------------------------

static ULONGLONG NextRng(ULONGLONG State) {
    State ^= State << 13;
    State ^= State >> 7;
    State ^= State << 17;
    return State;
}

static ULONG GetLineLen(PCWSTR S, ULONG Start) {
    ULONG W = 0;
    while (S[Start + W] && S[Start + W] != L'\n')
        W++;
    return W < LINE_WIDTH ? W : LINE_WIDTH;
}

VOID NtProcessStartup(PPEB Peb) {
    UNREFERENCED_PARAMETER(Peb);

    QUOTE_SET Set = { 0 };
    PVOID View;
    ULONG CharCount;
    HANDLE SectionHandle;

    NTSTATUS Status = MapQuotesFile(L"\\SystemRoot\\System32\\quotes.txt",
                                    &View,
                                    &CharCount,
                                    &SectionHandle);
    if (!NT_SUCCESS(Status))
        RtlExitUserProcess(Status);

    Set.ViewBase = View;
    Set.SectionHandle = SectionHandle;

    Status = ParseQuotes(reinterpret_cast<PWSTR>(View), CharCount, &Set);
    if (!NT_SUCCESS(Status)) {
        FreeQuoteSet(&Set);
        RtlExitUserProcess(Status);
    }

    HANDLE WatcherThread;
    NTSTATUS WatcherSt = NtCreateThreadEx(
        &WatcherThread, THREAD_ALL_ACCESS, NULL, NtCurrentProcess(),
        KeyboardWatcher, NULL, THREAD_CREATE_FLAGS_NONE, 0, 0, 0, NULL);
    if (NT_SUCCESS(WatcherSt)) {
        NtClose(WatcherThread);
    }

    LARGE_INTEGER Seed;
    RtlQueryPerformanceCounter(&Seed);

    ULONGLONG Rng = static_cast<ULONGLONG>(Seed.QuadPart);
    ULONGLONG Limit = MAXULONGLONG - (MAXULONGLONG % Set.Count);
    do {
        Rng = NextRng(Rng);
    } while (Rng >= Limit);

    PCWSTR Selected = Set.Entries[Rng % Set.Count];
    ULONG_PTR Len = wcslen(Selected);

    WCHAR Blank[LINE_WIDTH + 1];
    WCHAR String[LINE_WIDTH + 1];

    ULONG Slot = 0;
    ULONG CurrentLineLen = GetLineLen(Selected, 0);

    for (ULONG j = 0; j < CurrentLineLen; j++) {
        Blank[j] = L' ';
        String[j] = L' ';
    }
    Blank[CurrentLineLen] = L'\0';
    String[CurrentLineLen] = L'\0';

    UNICODE_STRING Text;
    LARGE_INTEGER TimeOut;

    for (ULONG i = 0; i < Len; i++) {
        if (Slot == CurrentLineLen || Selected[i] == L'\n') {
            TimeOut.QuadPart = 2 * 1000 * -10'000LL;
            RtlDelayExecution(FALSE, &TimeOut);

            WCHAR SavedChar = Blank[Slot];
            Blank[Slot] = L'\0';
            UNICODE_STRING BlankText;
            RtlInitUnicodeStringEx(&BlankText, Blank);
            NtDrawText(&BlankText);
            Blank[Slot] = SavedChar;

            if (Selected[i] == L'\n')
                CurrentLineLen = GetLineLen(Selected, i + 1);
            else
                CurrentLineLen = LINE_WIDTH;

            Slot = 0;
            for (ULONG j = 0; j < CurrentLineLen; j++) {
                Blank[j] = L' ';
                String[j] = L' ';
            }
            Blank[CurrentLineLen] = L'\0';
            String[CurrentLineLen] = L'\0';

            if (Selected[i] == L'\n')
                continue;
        }

        String[Slot] = Selected[i];
        RtlInitUnicodeStringEx(&Text, String);
        NtDrawText(&Text);
        Slot++;

        if (Selected[i] == L',' || Selected[i] == L'.' || Selected[i] == L'!' ||
            Selected[i] == L'?') {
            TimeOut.QuadPart = 150 * -10'000LL;
        } else {
            TimeOut.QuadPart = 50 * -10'000LL;
        }
        RtlDelayExecution(FALSE, &TimeOut);
    }

    TimeOut.QuadPart = 1 * 1000 * -10'000LL;
    RtlDelayExecution(FALSE, &TimeOut);

    FreeQuoteSet(&Set);
    RtlExitUserProcess(STATUS_SUCCESS);
}