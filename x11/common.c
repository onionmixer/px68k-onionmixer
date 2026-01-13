// ---------------------------------------------------------------------------------------
//  COMMON - 標準ヘッダ群（COMMON.H）とエラーダイアログ表示とか
// ---------------------------------------------------------------------------------------
//#include	<windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <iconv.h>
#include <errno.h>
#ifdef ANDROID
#include <android/log.h>
#endif

//#include	"sstp.h"

//extern HWND hWndMain;
extern const char PrgTitle[];

// P6L: PX68K_LOG
//      ~ ~   ~
#define P6L_LEN 256
char p6l_buf[P6L_LEN];

void Error(const char* s)
{
	printf("%s Error: %s\n", PrgTitle, s);

//	SSTP_SendMes(SSTPMES_ERROR);

//	MessageBox(hWndMain, s, title, MB_ICONERROR | MB_OK);
}

// log for debug
void p6logd(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(p6l_buf, P6L_LEN, fmt, args);
	va_end(args);

#if defined(ANDROID)
	__android_log_write(ANDROID_LOG_DEBUG, "Tag", p6l_buf);
#elif defined(PSP)
	printf("%s", p6l_buf);
#else
	printf("%s", p6l_buf);
#endif
}

// Convert UTF-8 string to Shift-JIS
// Returns 0 on success, -1 on failure
int utf8_to_sjis(const char *utf8_str, char *sjis_str, size_t sjis_size)
{
	iconv_t cd;
	char *inbuf, *outbuf;
	size_t inbytesleft, outbytesleft;
	size_t ret;

	if (utf8_str == NULL || sjis_str == NULL || sjis_size == 0) {
		return -1;
	}

	// Open iconv conversion descriptor
	cd = iconv_open("SHIFT_JIS//TRANSLIT//IGNORE", "UTF-8");
	if (cd == (iconv_t)-1) {
		// Fallback: just copy the string if iconv is not available
		strncpy(sjis_str, utf8_str, sjis_size - 1);
		sjis_str[sjis_size - 1] = '\0';
		return -1;
	}

	inbuf = (char *)utf8_str;
	inbytesleft = strlen(utf8_str);
	outbuf = sjis_str;
	outbytesleft = sjis_size - 1;  // Reserve space for null terminator

	ret = iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft);

	// Null-terminate the output
	*outbuf = '\0';

	iconv_close(cd);

	// If conversion had errors but produced some output, still return success
	// The //TRANSLIT//IGNORE flags should handle most problematic characters
	return (ret == (size_t)-1 && outbuf == sjis_str) ? -1 : 0;
}
