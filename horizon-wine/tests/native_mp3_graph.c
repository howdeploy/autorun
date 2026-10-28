/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#define COBJMACROS
#include "dshow.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void check(HRESULT hr)
{
    if (FAILED(hr))
    {
        fprintf(stderr, "DirectShow failed: %#lx\n", hr);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    IGraphBuilder *graph;
    IMediaControl *control;
    IMediaSeeking *seeking;
    IMediaEvent *events;
    WCHAR filename[1024];
    LONGLONG duration;
    unsigned int i;

    assert(argc == 2);
    assert(MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, filename, ARRAY_SIZE(filename)));
    check(CoInitialize(NULL));
    check(CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
            &IID_IGraphBuilder, (void **)&graph));
    check(IGraphBuilder_RenderFile(graph, filename, NULL));
    /* The runner disables winegstreamer to exercise the native MP3 fallback. */
    assert(!GetModuleHandleW(L"winegstreamer.dll"));
    check(IGraphBuilder_QueryInterface(graph, &IID_IMediaControl, (void **)&control));
    check(IGraphBuilder_QueryInterface(graph, &IID_IMediaSeeking, (void **)&seeking));
    check(IGraphBuilder_QueryInterface(graph, &IID_IMediaEvent, (void **)&events));
    check(IMediaSeeking_GetDuration(seeking, &duration));
    assert(duration > 0);
    printf("Native MP3 duration: %lld ticks\n", duration);

    for (i = 0; i < 3; ++i)
    {
        LONGLONG start = i == 1 ? 10000000 : 0, stop = start + 2500000;
        LONG code;
        LONG_PTR first, second;
        DWORD deadline;
        BOOL complete = FALSE;

        if (i == 2) check(IMediaControl_Stop(control));
        check(IMediaSeeking_SetPositions(seeking, &start, AM_SEEKING_AbsolutePositioning,
                &stop, AM_SEEKING_AbsolutePositioning));
        while (IMediaEvent_GetEvent(events, &code, &first, &second, 0) == S_OK)
            check(IMediaEvent_FreeEventParams(events, code, first, second));
        check(IMediaControl_Run(control));
        deadline = GetTickCount() + 5000;
        while ((LONG)(deadline - GetTickCount()) > 0)
        {
            HRESULT hr = IMediaEvent_GetEvent(events, &code, &first, &second, 100);
            if (hr == E_ABORT) continue;
            check(hr);
            if (code == EC_ERRORABORT) check((HRESULT)first);
            complete = code == EC_COMPLETE;
            check(IMediaEvent_FreeEventParams(events, code, first, second));
            if (complete) break;
        }
        assert(complete);
        printf("Native MP3 segment %u completed\n", i + 1);
    }
    check(IMediaControl_Stop(control));
    IMediaEvent_Release(events);
    IMediaSeeking_Release(seeking);
    IMediaControl_Release(control);
    IGraphBuilder_Release(graph);
    CoUninitialize();
    puts("Native MP3 graph: playback, EOS, seek and restart passed");
    return 0;
}
