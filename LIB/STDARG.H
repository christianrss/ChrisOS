#ifndef STDARG_H
#define STDARG_H
typedef long va_list;
void va_start(va_list ap, int last);
long va_arg(va_list ap, int dummy);
void va_end(va_list ap);
#endif
