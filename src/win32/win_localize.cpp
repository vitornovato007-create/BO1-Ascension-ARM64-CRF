#include "win_localize.h"
#include <universal/com_fileaccess.h>
#include <universal/assertive.h>
#include <stringed/stringed_hooks.h>
#include <universal/q_parse.h>
#include <universal/q_shared.h>
#include <cstring>
#ifdef BO1_WEB
#include <unistd.h>
#include <sys/stat.h>
#endif

struct// $8CB265A9D3778DFC1F2AA7A5F0192391 // sizeof=0x8
{                                       // XREF: .data:localization/r
    char *language;                     // XREF: Win_InitLocalization(void)+6/w
    char *strings;                      // XREF: Win_InitLocalization(void)+10/w
} localization;

char language_buffer[4096];

int Win_InitLocalization()
{
    signed int size; // [esp+0h] [ebp-10h]
    int sizea; // [esp+0h] [ebp-10h]
    FILE *fp; // [esp+4h] [ebp-Ch]
    int i; // [esp+8h] [ebp-8h]
    int lang; // [esp+Ch] [ebp-4h] BYREF

#ifdef BO1_WEB
    char localization_path[2048] = {0};
    char cwd[1024] = {0};
    const char *cwd_result = getcwd(cwd, sizeof(cwd));
    fprintf(stderr, "[BO1-LOCALIZATION] Win_InitLocalization enter cwd=%s\\n",
            cwd_result ? cwd : "<getcwd failed>");
#endif
    localization.language = 0;
    localization.strings = 0;
#ifdef BO1_WEB
    if (cwd_result && snprintf(localization_path, sizeof(localization_path),
                               "%s/localization.txt", cwd) < (int)sizeof(localization_path))
    {
        struct stat file_info;
        const int relative_access = access("localization.txt", R_OK);
        const int absolute_access = access(localization_path, R_OK);
        const int absolute_stat = stat(localization_path, &file_info);
        fprintf(stderr,
                "[BO1-LOCALIZATION] path probe relative_access=%d absolute_access=%d absolute_stat=%d absolute_path='%s'\\n",
                relative_access, absolute_access, absolute_stat, localization_path);
        fp = FS_FileOpenReadText(localization_path);
        fprintf(stderr, "[BO1-LOCALIZATION] absolute open fp=%p\\n", (void *)fp);
    }
    else
        fp = NULL;
    if (!fp)
    {
        fprintf(stderr, "[BO1-LOCALIZATION] trying relative localization.txt\\n");
        fp = FS_FileOpenReadText("localization.txt");
        fprintf(stderr, "[BO1-LOCALIZATION] relative open fp=%p\\n", (void *)fp);
    }
#else
    fp = FS_FileOpenReadText("localization.txt");
#endif

    if (!fp)
    {
        iassert(0); // ADD: you probably need to change the working dir!
        return 0;
    }

    size = FS_FileGetFileSize(fp);
#ifdef BO1_WEB
    fprintf(stderr, "[BO1-LOCALIZATION] file size=%d\\n", size);
#endif
    if (size >= 4096
        && !Assert_MyHandler(
            "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_localize.cpp",
            39,
            0,
            "%s",
            "size < LANGUAGE_BUF_SIZE"))
    {
        __debugbreak();
    }
    localization.language = language_buffer;
    sizea = FS_FileRead(language_buffer, size, fp);
    FS_FileClose(fp);
#ifdef BO1_WEB
    fprintf(stderr, "[BO1-LOCALIZATION] read bytes=%d language_ptr=%p\\n", sizea, (void *)localization.language);
    if (sizea > 0) {
        int dump_count = sizea < 16 ? sizea : 16;
        int dump_i;
        fprintf(stderr, "[BO1-LOCALIZATION] first bytes:");
        for (dump_i = 0; dump_i < dump_count; ++dump_i)
            fprintf(stderr, " %02X", (unsigned char)language_buffer[dump_i]);
        fprintf(stderr, "\\n");
    }
#endif
    if (sizea)
    {
        localization.language[sizea] = 0;
        lang = 0;
        for (i = 0; localization.language[i]; ++i)
        {
            if (localization.language[i] == 10)
            {
                localization.language[i] = 0;
#ifdef BO1_WEB
                // web: the retail localization.txt has CRLF line ends; MSVC's text-mode fopen ("rt") drops the CR,
                // POSIX fopen does not, and the language name ("english\r") then names a zone folder that is not there
                if (i > 0 && localization.language[i - 1] == 13)
                    localization.language[i - 1] = 0;
#endif
                localization.strings = &localization.language[i + 1];
                SEH_GetLanguageIndexForName(localization.language, &lang);
#ifdef BO1_WEB
                fprintf(stderr, "[BO1-LOCALIZATION] parsed language='%s' index=%d strings=%p\\n",
                        localization.language, lang, (void *)localization.strings);
#endif
                return lang;
            }
        }
#ifdef BO1_WEB
        fprintf(stderr, "[BO1-LOCALIZATION] no LF found; language buffer retained, strings=%p\\n",
                (void *)localization.strings);
#endif
        return lang;
    }
    else
    {
#ifdef BO1_WEB
        fprintf(stderr, "[BO1-LOCALIZATION] zero-byte read; clearing language pointer\\n");
#endif
        localization.language = 0;
        return 0;
    }
}

void __cdecl Win_ShutdownLocalization()
{
#ifdef BO1_WEB
    fprintf(stderr, "[BO1-LOCALIZATION] Win_ShutdownLocalization language=%p strings=%p\\n",
            (void *)localization.language, (void *)localization.strings);
#endif
    localization.language = 0;
    localization.strings = 0;
}

char *__cdecl Win_LocalizeRef(const char *ref)
{
    const char *v1; // eax
    const char *v3; // eax
    const char *strings; // [esp+14h] [ebp-Ch] BYREF
    int useRef; // [esp+18h] [ebp-8h]
    const char *token; // [esp+1Ch] [ebp-4h]

    Com_BeginParseSession("localization");
    strings = localization.strings;
    do
    {
        token = (const char *)Com_Parse(&strings);
        if ( !*token )
        {
            Com_EndParseSession();
            v1 = va("unlocalized: %s", ref);
            if ( !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_localize.cpp", 112, 0, v1) )
                __debugbreak();
            return Win_CopyLocalizationString(ref);
        }
        useRef = strcmp(token, ref) == 0;
        token = (const char *)Com_Parse(&strings);
        if ( !*token )
        {
            Com_EndParseSession();
            v3 = va("missing value: %s", ref);
            if ( !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_localize.cpp", 121, 0, v3) )
                __debugbreak();
            return Win_CopyLocalizationString(ref);
        }
    }
    while ( !useRef );
    Com_EndParseSession();
    return Win_CopyLocalizationString(token);
}

char *__cdecl Win_CopyLocalizationString(const char *string)
{
    return va("%s", string);
}

char *__cdecl Win_GetLanguage()
{
#ifdef BO1_WEB
    fprintf(stderr, "[BO1-LOCALIZATION] Win_GetLanguage language=%p strings=%p\\n",
            (void *)localization.language, (void *)localization.strings);
#endif
    if ( !localization.language
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_localize.cpp",
                    140,
                    0,
                    "%s",
                    "localization.language") )
    {
        __debugbreak();
    }
    return localization.language;
}

