// disc_import_jni.cpp - the launcher's way into disc_extract (com.shiinobu.basara.DiscImport).
#include "disc_extract.h"

#include <jni.h>
#include <unistd.h>

#include <cerrno>
#include <string>

namespace {

disc::ReadAt FileReader(int fd) {
    return [fd](uint64_t offset, void* out, size_t size) {
        auto* bytes = static_cast<char*>(out);
        while (size != 0) {
            const ssize_t got = pread64(fd, bytes, size, static_cast<off64_t>(offset));
            if (got < 0 && errno == EINTR) continue;
            if (got <= 0) return false;
            bytes += got;
            offset += static_cast<uint64_t>(got);
            size -= static_cast<size_t>(got);
        }
        return true;
    };
}

}  // namespace

extern "C" {

JNIEXPORT jbyteArray JNICALL Java_com_shiinobu_basara_DiscImport_readHeader(JNIEnv* env, jclass, jint fd) {
    uint8_t header[8];
    std::string message;
    if (!disc::ReadHeader(FileReader(fd), header, message)) {
        env->ThrowNew(env->FindClass("java/io/IOException"), message.c_str());
        return nullptr;
    }
    jbyteArray result = env->NewByteArray(sizeof(header));
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, sizeof(header), reinterpret_cast<const jbyte*>(header));
    }
    return result;
}

JNIEXPORT jstring JNICALL Java_com_shiinobu_basara_DiscImport_extract(JNIEnv* env, jclass, jint fd, jstring outDir,
                                                                       jlong freeBytes, jobject listener) {
    const char* chars = env->GetStringUTFChars(outDir, nullptr);
    const std::string path(chars);
    env->ReleaseStringUTFChars(outDir, chars);
    const jmethodID onProgress = env->GetMethodID(env->GetObjectClass(listener), "onProgress", "(JJ)Z");
    const disc::Result result = disc::Extract(FileReader(fd), path, static_cast<uint64_t>(freeBytes),
        [&](uint64_t done, uint64_t total) {
            return env->CallBooleanMethod(listener, onProgress, static_cast<jlong>(done), static_cast<jlong>(total)) == JNI_TRUE;
        });
    switch (result.status) {
        case disc::Result::Status::Ok: return nullptr;
        case disc::Result::Status::Cancelled: return env->NewStringUTF("");
        default: return env->NewStringUTF(result.message.c_str());
    }
}

}  // extern "C"
