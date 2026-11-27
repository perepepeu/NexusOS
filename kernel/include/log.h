#ifndef NEXUS_LOG_H
#define NEXUS_LOG_H

enum log_level { LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };
void log_init(void);
void log_set_level(enum log_level minimum);
/* Formatos: %s, %u/%x (unsigned int), %llu (unsigned long long), %p e %%. */
void log_write(enum log_level level, const char *module, const char *format, ...);

#define log_debug(module, ...) log_write(LOG_DEBUG, module, __VA_ARGS__)
#define log_info(module, ...) log_write(LOG_INFO, module, __VA_ARGS__)
#define log_warn(module, ...) log_write(LOG_WARN, module, __VA_ARGS__)
#define log_error(module, ...) log_write(LOG_ERROR, module, __VA_ARGS__)

#endif
