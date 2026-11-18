#include "../command.h"
#include "../desktop/fd.h"
#include <platform/ZCFileUtils.h>

namespace {

class ZonixFiles final : public zocos::FileUtils {
    bool readBinaryFileImpl(const mstd::string& path, mstd::vector<unsigned char>& out) const override {
        desktop_ipc::Fd file(sys_open(mutable_path(path), O_RDONLY | O_CLOEXEC));
        DescriptorStat stat{};
        if (file.get() < 0 || sys_fstat(file.get(), &stat) < 0 || stat.type != FILE_TYPE_FILE ||
            stat.size > MAX_ASSET_SIZE)
            return false;
        out.resize(stat.size);
        mstd::size_t done = 0;
        while (done < stat.size) {
            long count = sys_read(file.get(), out.data() + done, stat.size - done);
            if (count <= 0) {
                out.clear();
                return false;
            }
            done += count;
        }
        return true;
    }
    bool writeBinaryFileImpl(const mstd::string& path, const unsigned char* data, mstd::size_t size) const override {
        if (size > MAX_ASSET_SIZE)
            return false;
        int fd = sys_open(mutable_path(path), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC);
        if (fd < 0)
            return false;
        long error = command_write_all(fd, data, size);
        return sys_close(fd) == 0 && error == 0;
    }
    mstd::string parentPathImpl(const mstd::string& path) const override {
        for (auto i = path.size(); i > 0; --i)
            if (path[i - 1] == '/')
                return i == 1 ? mstd::string("/") : path.substr(0, i - 1);
        return {};
    }
    bool isAbsolutePathImpl(const mstd::string& path) const override { return !path.empty() && path[0] == '/'; }
    bool isFileExistImpl(const mstd::string& path) const override { return type(path) == FILE_TYPE_FILE; }
    bool isDirectoryExistImpl(const mstd::string& path) const override { return type(path) == FILE_TYPE_DIRECTORY; }
    mstd::uintmax_t getFileSizeImpl(const mstd::string& path) const override {
        FileStat stat{};
        return sys_stat(mutable_path(path), &stat) == 0 && stat.type == FILE_TYPE_FILE ? stat.size : 0;
    }
    mstd::string getWritablePathImpl() const override { return "/home"; }
    bool createDirectoryImpl(const mstd::string& path) const override { return sys_mkdir(mutable_path(path)) == 0; }
    bool removeDirectoryImpl(const mstd::string& path) const override { return sys_rmdir(mutable_path(path)) == 0; }
    bool removeFileImpl(const mstd::string& path) const override { return sys_unlink(mutable_path(path)) == 0; }
    bool renameFileImpl(const mstd::string&, const mstd::string&) const override { return false; }
    mstd::string normalizePathImpl(const mstd::string& path) const override {
        if (path.empty() || isAbsolutePathImpl(path))
            return path;
        char cwd[PROCESS_MAX_PATH]{};
        return sys_getcwd(cwd, sizeof(cwd)) == 0 ? joinPathImpl(cwd, path) : mstd::string{};
    }
    mstd::string joinPathImpl(const mstd::string& directory, const mstd::string& filename) const override {
        if (directory.empty() || isAbsolutePathImpl(filename))
            return filename;
        return directory[directory.size() - 1] == '/' ? directory + filename : directory + "/" + filename;
    }

    static constexpr unsigned int MAX_ASSET_SIZE = 2 * 1024 * 1024;

    static char* mutable_path(const mstd::string& path) { return const_cast<char*>(path.c_str()); }
    static unsigned int type(const mstd::string& path) {
        FileStat stat{};
        return sys_stat(mutable_path(path), &stat) == 0 ? stat.type : 0;
    }
};

}  // namespace

zocos::FileUtils& zocos::FileUtils::getInstance() {
    static ZonixFiles files;
    return files;
}
