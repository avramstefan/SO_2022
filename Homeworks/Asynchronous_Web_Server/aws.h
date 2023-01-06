/*
 * Asynchronous Web Server - header file (macros and structures)
 *
 * 2011-2017, Operating Systems
 */

#ifndef AWS_H_
#define AWS_H_		1

#ifdef __cplusplus
extern "C" {
#endif

#define FILE_NOT_FOUND -1
#define FILE_FOUND 0
#define HTTP_NOT_FOUND_MSG "HTTP/1.0 404 Not Found\r\n\r\n"
#define HTTP_OK_MSG "HTTP/1.0 200 OK\r\n\r\n"
#define STATIC 1
#define DYNAMIC 0

#define AWS_LISTEN_PORT		8888
#define AWS_DOCUMENT_ROOT	"./"
#define AWS_REL_STATIC_FOLDER	"static/"
#define AWS_REL_DYNAMIC_FOLDER	"dynamic/"
#define AWS_ABS_STATIC_FOLDER	(AWS_DOCUMENT_ROOT AWS_REL_STATIC_FOLDER)
#define AWS_ABS_DYNAMIC_FOLDER	(AWS_DOCUMENT_ROOT AWS_REL_DYNAMIC_FOLDER)

#ifdef __cplusplus
}
#endif

#endif /* AWS_H_ */
