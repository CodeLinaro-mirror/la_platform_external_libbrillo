ACLOCAL_AMFLAGS := -I m4

AM_CFLAGS := -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -g -O0 \
        $(LOCBASEUTIL_CFLAGS) \
        -fno-inline \
        -fno-short-enums \
        -fpic \
        -D_GNU_SOURCE \
        -DHAVE_CONFIG_H=1 \
        -DBUILD_DIVSUFSORT64 \
        -D_ANDROID_HOST_ \
        -D_ANDROID_ \
        -I../brillo

AM_CPPFLAGS := \
              -I. \
              -USE_GLIB \
              -I$(WORKSPACE)/OTA/streaming/external/libchrome \
              -I$(WORKSPACE)/OTA/streaming/frameworks/native/include \
              -I$(WORKSPACE)/OTA/streaming/external/libbrillo \
              -I$(WORKSPACE)/OTA/streaming/external/modp_b64 \
              -I$(WORKSPACE)/OTA/streaming/external/curl/include \
              -I$(WORKSPACE)/OTA/streaming/external/boringssl/src/include \
              -I$(WORKSPACE)/system/core/base/include \
              -I$(WORKSPACE)/system/core/include \
              -D_FILE_OFFSET_BITS=64 \
              -D_USE_RTTI_FOR_TYPE_TAGS \
              -D_ANDROID_HOST_ \
              -D_ANDROID_ \
              -DUSE_LE_MODE \
              -std=c++11


AUTOMAKE_OPTIONS = subdir-objects

AM_LDFLAGS := -shared -version-info 1:0:0

if TARGET_NAND_BOOT
AM_CFLAGS += -DUSE_MTD=1
AM_CPPFLAGS += -DUSE_MTD=1
endif


libbrillo_core_la_SOURCES := brillo/backoff_entry.cc \
    brillo/data_encoding.cc \
    brillo/errors/error.cc \
    brillo/errors/error_codes.cc \
    brillo/flag_helper.cc \
    brillo/key_value_store.cc \
    brillo/message_loops/base_message_loop.cc \
    brillo/message_loops/message_loop.cc \
    brillo/message_loops/message_loop_utils.cc \
    brillo/mime_utils.cc \
    brillo/osrelease_reader.cc \
    brillo/process.cc \
    brillo/process_information.cc \
    brillo/secure_blob.cc \
    brillo/strings/string_utils.cc \
    brillo/syslog_logging.cc \
    brillo/type_name_undecorate.cc \
    brillo/url_utils.cc \
    brillo/userdb_utils.cc \
    brillo/value_conversion.cc

libbrillo_linux_la_SOURCES := brillo/asynchronous_signal_handler.cc \
    brillo/daemons/daemon.cc \
    brillo/file_utils.cc \
    brillo/process_reaper.cc

libbrillo_http_la_SOURCES := brillo/http/curl_api.cc \
    brillo/http/http_connection_curl.cc \
    brillo/http/http_form_data.cc \
    brillo/http/http_request.cc \
    brillo/http/http_transport.cc \
    brillo/http/http_transport_curl.cc \
    brillo/http/http_utils.cc

libbrillo_stream_la_SOURCES := brillo/streams/file_stream.cc \
    brillo/streams/input_stream_set.cc \
    brillo/streams/memory_containers.cc \
    brillo/streams/memory_stream.cc \
    brillo/streams/openssl_stream_bio.cc \
    brillo/streams/stream.cc \
    brillo/streams/stream_errors.cc \
    brillo/streams/stream_utils.cc \
    brillo/streams/tls_stream.cc

libbrillo_test_helpers_sources := \
    brillo/http/http_connection_fake.cc \
    brillo/http/http_transport_fake.cc \
    brillo/message_loops/fake_message_loop.cc \
    brillo/streams/fake_stream.cc \
    brillo/unittest_utils.cc


libbrillo_binder_la_SOURCES := brillo/binder_watcher.cc

h_sources := brillo/binder_watcher.h
library_includedir := $(pkgincludedir)

library_include_HEADERS := $(h_sources)

lib_LTLIBRARIES := libbrillo.la
libbrillo_la_SOURCES := $(libbrillo_core_la_SOURCES)

libbrillo_la_SOURCES += $(libbrillo_linux_la_SOURCES)
libbrillo_la_SOURCES += $(libbrillo_http_la_SOURCES)
libbrillo_la_SOURCES += $(libbrillo_stream_la_SOURCES)
libbrillo_la_SOURCES += $(libbrillo_binder_la_SOURCES)
libbrillo_la_SOURCES += $(libbrillo_test_helpers_sources)
libbrillo_la_SOURCES += $(h_sources)

#if USE_GLIB
libbrillo_la_CFLAGS := $(AM_CFLAGS) -DUSE_GLIB @GLIB_CFLAGS@
libbrillo_la_CPPFLAGS := $(AM_CPPFLAGS) -fpermissive -DUSE_GLIB @GLIB_CFLAGS@ 
libbrillo_la_LIBADD := -lm -lssl -lcrypto -lchrome -lpthread -lbinderstreaming -lcurl @GLIB_LIBS@
#else
libbrillo_la_CFLAGS := $(AM_CFLAGS)
libbrillo_la_CPPFLAGS := $(AM_CPPFLAGS) -fpermissive
libbrillo_la_LIBADD := -lm -lssl -lcrypto -lchrome -lpthread -lbinderstreaming -lcurl
#endif

if !BUILD_HOST
libbrillo_la_LIBADD += -ltelaf_flash_access
endif

brillo_includedir=$(includedir)/libbrillo
brillo_include_HEADERS := brillo/flag_helper.h
