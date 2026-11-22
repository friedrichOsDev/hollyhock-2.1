// CPV v1.3 native host encoder.
// Video decoding and palette generation are delegated to FFmpeg; all pixel mapping,
// packet construction, motion analysis, compression, and container writing are native C++.
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <vector>
#include <zlib.h>
#include <lz4hc.h>

namespace fs = std::filesystem;

static constexpr uint32_t MAX_WIDTH = 528;
static constexpr uint32_t MAX_HEIGHT = 320;
static constexpr uint32_t MAX_FPS = 30;
static constexpr uint32_t MAX_RUN = 8191;
static constexpr uint32_t MAX_MASK_SPAN = 65535;
static constexpr uint8_t FLAG_LZ4 = 0x01;
static constexpr uint8_t FLAG_DEFLATE = 0x02;
static constexpr uint8_t FLAG_MOTION = 0x04;
static constexpr uint8_t FRAME_I = 0;
static constexpr uint8_t FRAME_P = 1;
static constexpr uint8_t OP_LITERAL = 0;
static constexpr uint8_t OP_REPEAT = 1;
static constexpr uint8_t OP_SKIP = 2;
static constexpr uint8_t OP_RAW565 = 3;
static constexpr uint8_t OP_MASK = 4;
static constexpr std::array<const char *, 5> OP_NAMES = {
    "INDEX_LITERAL", "INDEX_REPEAT", "SKIP", "RAW565", "DELTA_MASK"
};

struct Options {
    std::string input, output, ffmpeg = "ffmpeg", ffprobe = "ffprobe";
    uint32_t width = 0, height = 0;
    uint32_t fps_num = 0, fps_den = 1;
    int palette_quality = 75, preprocess_quality = 0;
    double keyframe_interval = 2.0;
    uint32_t background = 0;
    bool fit_stretch = false;
    bool no_motion = false, no_delta_mask = false, no_compression = false;
};

struct PacketStats {
    std::array<uint64_t, 5> count{};
    std::array<uint64_t, 5> bytes{};
    void add(unsigned op, size_t n) { count[op]++; bytes[op] += n; }
    void merge(const PacketStats &other) {
        for (size_t i = 0; i < count.size(); ++i) {
            count[i] += other.count[i];
            bytes[i] += other.bytes[i];
        }
    }
};

struct ChildProcess {
    pid_t pid = -1;
    int out_fd = -1;
    int in_fd = -1;
    FILE *err_file = nullptr;
};

static void close_fd(int &fd) {
    if (fd >= 0) { close(fd); fd = -1; }
}

static std::string read_child_error(FILE *file) {
    if (!file) return {};
    fflush(file);
    fseeko(file, 0, SEEK_SET);
    std::string result;
    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0)
        result.append(buffer, n);
    return result;
}

static ChildProcess launch_process(const std::vector<std::string> &args, bool pipe_stdin) {
    if (args.empty()) throw std::runtime_error("empty child process command");
    int out_pipe[2];
    int in_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0) throw std::runtime_error("pipe failed: " + std::string(strerror(errno)));
    if (pipe_stdin && pipe(in_pipe) != 0) {
        close(out_pipe[0]); close(out_pipe[1]);
        throw std::runtime_error("stdin pipe failed: " + std::string(strerror(errno)));
    }
    FILE *errors = tmpfile();
    if (!errors) {
        close(out_pipe[0]); close(out_pipe[1]);
        if (in_pipe[0] >= 0) { close(in_pipe[0]); close(in_pipe[1]); }
        throw std::runtime_error("could not create temporary stderr file");
    }

    pid_t pid = fork();
    if (pid < 0) {
        fclose(errors); close(out_pipe[0]); close(out_pipe[1]);
        if (in_pipe[0] >= 0) { close(in_pipe[0]); close(in_pipe[1]); }
        throw std::runtime_error("fork failed: " + std::string(strerror(errno)));
    }
    if (pid == 0) {
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(fileno(errors), STDERR_FILENO);
        if (pipe_stdin) dup2(in_pipe[0], STDIN_FILENO);
        else {
            int null_fd = open("/dev/null", O_RDONLY);
            if (null_fd >= 0) { dup2(null_fd, STDIN_FILENO); close(null_fd); }
        }
        close(out_pipe[0]); close(out_pipe[1]);
        if (in_pipe[0] >= 0) { close(in_pipe[0]); close(in_pipe[1]); }
        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(out_pipe[1]);
    if (in_pipe[0] >= 0) close(in_pipe[0]);
    ChildProcess child;
    child.pid = pid;
    child.out_fd = out_pipe[0];
    child.in_fd = in_pipe[1];
    child.err_file = errors;
    return child;
}

static void write_fd_all(int fd, const uint8_t *data, size_t size) {
    size_t pos = 0;
    while (pos < size) {
        ssize_t n = write(fd, data + pos, size - pos);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("failed writing child process input");
        pos += static_cast<size_t>(n);
    }
}

static int finish_process(ChildProcess &child, std::string &error_text) {
    close_fd(child.in_fd);
    close_fd(child.out_fd);
    int status = 0;
    while (waitpid(child.pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        child.pid = -1;
        throw std::runtime_error("waitpid failed");
    }
    child.pid = -1;
    error_text = read_child_error(child.err_file);
    if (child.err_file) { fclose(child.err_file); child.err_file = nullptr; }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128;
}

static void abort_process(ChildProcess &child) {
    close_fd(child.in_fd);
    close_fd(child.out_fd);
    if (child.pid > 0) { kill(child.pid, SIGTERM); waitpid(child.pid, nullptr, 0); child.pid = -1; }
    if (child.err_file) { fclose(child.err_file); child.err_file = nullptr; }
}

static std::vector<uint8_t> run_capture(
    const std::vector<std::string> &args,
    const std::vector<uint8_t> *stdin_data,
    const std::string &description
) {
    ChildProcess child = launch_process(args, stdin_data != nullptr);
    try {
        if (stdin_data) {
            write_fd_all(child.in_fd, stdin_data->data(), stdin_data->size());
            close_fd(child.in_fd);
        }
        std::vector<uint8_t> output;
        uint8_t buffer[65536];
        for (;;) {
            ssize_t n = read(child.out_fd, buffer, sizeof(buffer));
            if (n < 0 && errno == EINTR) continue;
            if (n < 0) throw std::runtime_error("failed reading " + description);
            if (n == 0) break;
            output.insert(output.end(), buffer, buffer + n);
        }
        std::string error;
        int code = finish_process(child, error);
        if (code != 0)
            throw std::runtime_error(description + " failed (exit " + std::to_string(code) + ")" +
                (error.empty() ? "" : ": " + error));
        return output;
    } catch (...) {
        abort_process(child);
        throw;
    }
}

static bool read_exact_frame(int fd, std::vector<uint8_t> &buffer) {
    size_t offset = 0;
    while (offset < buffer.size()) {
        ssize_t n = read(fd, buffer.data() + offset, buffer.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) throw std::runtime_error("failed reading raw frames from FFmpeg");
        if (n == 0) {
            if (offset == 0) return false;
            throw std::runtime_error("FFmpeg returned a truncated raw frame");
        }
        offset += static_cast<size_t>(n);
    }
    return true;
}

static uint64_t parse_unsigned(const std::string &value, const std::string &name) {
    if (value.empty() || value[0] == '-') throw std::runtime_error("invalid " + name);
    size_t used = 0;
    unsigned long long n = 0;
    try { n = std::stoull(value, &used, 10); }
    catch (...) { throw std::runtime_error("invalid " + name + ": " + value); }
    if (used != value.size()) throw std::runtime_error("invalid " + name + ": " + value);
    return n;
}

static uint32_t parse_background(std::string value) {
    if (!value.empty() && value[0] == '#') value.erase(0, 1);
    if (value.size() != 6) throw std::runtime_error("background must be RRGGBB");
    char *end = nullptr;
    unsigned long n = strtoul(value.c_str(), &end, 16);
    if (!end || *end || n > 0xFFFFFF) throw std::runtime_error("background must contain six hexadecimal digits");
    return static_cast<uint32_t>(n);
}

static std::pair<uint32_t, uint32_t> parse_fps(const std::string &value) {
    uint64_t num = 0, den = 1;
    const size_t slash = value.find('/');
    if (slash != std::string::npos) {
        num = parse_unsigned(value.substr(0, slash), "fps numerator");
        den = parse_unsigned(value.substr(slash + 1), "fps denominator");
    } else {
        char *end = nullptr;
        long double target = strtold(value.c_str(), &end);
        if (!end || *end || !std::isfinite(static_cast<double>(target)) || target <= 0)
            throw std::runtime_error("fps must be a positive number or rational");
        long double best_error = std::numeric_limits<long double>::max();
        for (uint32_t d = 1; d <= 1000; ++d) {
            uint64_t n = static_cast<uint64_t>(llround(target * d));
            long double error = fabsl(target - static_cast<long double>(n) / d);
            if (error < best_error) { best_error = error; num = n; den = d; }
        }
    }
    if (!den || !num) throw std::runtime_error("fps must be greater than zero");
    uint64_t gcd = std::gcd(num, den);
    num /= gcd; den /= gcd;
    if (num > 65535 || den > 65535 || static_cast<long double>(num) / den > MAX_FPS)
        throw std::runtime_error("fps must be representable and no greater than 30");
    return {static_cast<uint32_t>(num), static_cast<uint32_t>(den)};
}

static void print_help() {
    std::cout <<
        "CPV v1.3 native video encoder\n"
        "Usage: cpv_convert INPUT --output FILE --width N --height N --fps N\n"
        "  --palette-quality 0..100     Palette budget (default 75)\n"
        "  --preprocess-quality 0..100  hqdn3d denoising strength (default 0)\n"
        "  --keyframe-interval SEC      I-frame interval, 0..10 (default 2)\n"
        "  --fit contain|stretch        Preserve aspect ratio or stretch (default contain)\n"
        "  --background RRGGBB         Letterbox colour (default 000000)\n"
        "  --no-global-motion           Disable v1.3 motion trials\n"
        "  --no-delta-mask              Disable mixed SKIP/data masks\n"
        "  --no-frame-compression       Disable LZ4-HC/DEFLATE\n"
        "  --no-lz4                     Alias for --no-frame-compression\n"
        "  --ffmpeg PATH                FFmpeg executable (default ffmpeg)\n"
        "  --ffprobe PATH               FFprobe executable (default ffprobe)\n"
        "  --self-test                  Run native encoder unit tests\n";
}

static Options parse_args(int argc, char **argv) {
    Options o;
    bool have_output = false, have_w = false, have_h = false, have_fps = false;
    auto value = [&](int &i, const std::string &name) -> std::string {
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + name);
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") { print_help(); std::exit(0); }
        else if (arg == "--self-test") { o.input = "__CPV_SELF_TEST__"; }
        else if (arg == "--output") { o.output = value(i, arg); have_output = true; }
        else if (arg == "--width") { o.width = static_cast<uint32_t>(parse_unsigned(value(i,arg), "width")); have_w = true; }
        else if (arg == "--height") { o.height = static_cast<uint32_t>(parse_unsigned(value(i,arg), "height")); have_h = true; }
        else if (arg == "--fps") {
            auto f = parse_fps(value(i,arg)); o.fps_num = f.first; o.fps_den = f.second; have_fps = true;
        }
        else if (arg == "--palette-quality") o.palette_quality = static_cast<int>(parse_unsigned(value(i,arg), "palette quality"));
        else if (arg == "--preprocess-quality") o.preprocess_quality = static_cast<int>(parse_unsigned(value(i,arg), "pre-process quality"));
        else if (arg == "--keyframe-interval") {
            std::string v = value(i,arg); char *end = nullptr; o.keyframe_interval = strtod(v.c_str(), &end);
            if (!end || *end || !std::isfinite(o.keyframe_interval)) throw std::runtime_error("invalid keyframe interval");
        }
        else if (arg == "--fit") {
            std::string v = value(i,arg);
            if (v == "stretch") o.fit_stretch = true;
            else if (v == "contain") o.fit_stretch = false;
            else throw std::runtime_error("--fit must be contain or stretch");
        }
        else if (arg == "--background") o.background = parse_background(value(i,arg));
        else if (arg == "--ffmpeg") o.ffmpeg = value(i,arg);
        else if (arg == "--ffprobe") o.ffprobe = value(i,arg);
        else if (arg == "--no-global-motion") o.no_motion = true;
        else if (arg == "--no-delta-mask") o.no_delta_mask = true;
        else if (arg == "--no-frame-compression" || arg == "--no-lz4") o.no_compression = true;
        else if (!arg.empty() && arg[0] == '-') throw std::runtime_error("unknown option: " + arg);
        else if (o.input.empty()) o.input = arg;
        else throw std::runtime_error("only one input file may be specified");
    }
    if (o.input == "__CPV_SELF_TEST__") return o;
    if (o.input.empty() || !have_output || !have_w || !have_h || !have_fps)
        throw std::runtime_error("input, --output, --width, --height and --fps are required (see --help)");
    if (o.width < 1 || o.width > MAX_WIDTH || o.height < 1 || o.height > MAX_HEIGHT)
        throw std::runtime_error("width and height must be within 1..528 and 1..320");
    if (o.palette_quality < 0 || o.palette_quality > 100 ||
        o.preprocess_quality < 0 || o.preprocess_quality > 100)
        throw std::runtime_error("quality values must be in the range 0..100");
    if (!(o.keyframe_interval > 0 && o.keyframe_interval <= 10))
        throw std::runtime_error("keyframe interval must be greater than 0 and no greater than 10 seconds");
    return o;
}

static std::string make_filter(
    const Options &o, int preprocess_quality
) {
    std::ostringstream filter;
    filter << "fps=" << o.fps_num << "/" << o.fps_den;
    if (o.fit_stretch) {
        filter << ",scale=" << o.width << ":" << o.height << ":flags=lanczos";
    } else {
        filter << ",scale=" << o.width << ":" << o.height
               << ":force_original_aspect_ratio=decrease:flags=lanczos";
        char bg[16];
        snprintf(bg, sizeof(bg), "0x%06X", o.background);
        filter << ",pad=" << o.width << ":" << o.height
               << ":(ow-iw)/2:(oh-ih)/2:color=" << bg;
    }
    if (preprocess_quality > 0) {
        double s = preprocess_quality / 100.0;
        filter.setf(std::ios::fixed);
        filter.precision(2);
        filter << ",hqdn3d=" << 4.0*s << ":" << 3.0*s << ":" << 6.0*s << ":" << 4.5*s;
    }
    filter << ",setsar=1";
    return filter.str();
}

static uint32_t palette_budget_for(int quality) {
    if (quality < 40) return 36;
    if (quality < 60) return 64;
    if (quality < 80) return 128;
    return 256;
}

static const std::array<std::array<uint8_t, 3>, 12> BASE_HUES = {{
    {{255,0,0}}, {{0,255,0}}, {{0,0,255}}, {{255,255,0}},
    {{0,255,255}}, {{255,0,255}}, {{255,128,0}}, {{255,0,128}},
    {{0,255,128}}, {{128,0,255}}, {{139,69,19}}, {{128,128,128}}
}};

static std::vector<uint8_t> make_seed_rgb24() {
    std::vector<uint8_t> pixels;
    pixels.reserve(108);
    for (const auto &h : BASE_HUES) {
        const uint8_t r=h[0], g=h[1], b=h[2];
        const uint8_t variants[3][3] = {
            {static_cast<uint8_t>(r/2), static_cast<uint8_t>(g/2), static_cast<uint8_t>(b/2)},
            {r,g,b},
            {static_cast<uint8_t>(r+(255-r)/2), static_cast<uint8_t>(g+(255-g)/2), static_cast<uint8_t>(b+(255-b)/2)}
        };
        for (const auto &v : variants) {
            pixels.push_back(v[0]); pixels.push_back(v[1]); pixels.push_back(v[2]);
        }
    }
    return pixels;
}

static uint16_t rgb565_truncate(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t rgb565_round(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t rr = (static_cast<uint32_t>(r) * 31 + 127) / 255;
    uint32_t gg = (static_cast<uint32_t>(g) * 63 + 127) / 255;
    uint32_t bb = (static_cast<uint32_t>(b) * 31 + 127) / 255;
    return static_cast<uint16_t>((rr << 11) | (gg << 5) | bb);
}

static std::vector<uint16_t> make_seed_palette() {
    const auto rgb = make_seed_rgb24();
    std::vector<uint16_t> result;
    result.reserve(36);
    for (size_t i = 0; i < rgb.size(); i += 3)
        result.push_back(rgb565_truncate(rgb[i], rgb[i+1], rgb[i+2]));
    return result;
}

static std::vector<uint16_t> build_palette(const Options &o, const std::string &source) {
    const uint32_t budget = palette_budget_for(o.palette_quality);
    const std::vector<uint8_t> seed = make_seed_rgb24();
    const std::string video_filter = make_filter(o, 0);
    std::ostringstream graph;
    graph << "[0:v]" << video_filter << "[video];"
          << "[1:v]scale=" << o.width << ":-1:flags=neighbor[seed];"
          << "[video][seed]vstack=inputs=2:shortest=0[combined];"
          << "[combined]palettegen=max_colors=" << budget
          << ":stats_mode=full:reserve_transparent=0[palette]";
    std::vector<std::string> cmd = {
        o.ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", source,
        "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "6x6",
        "-framerate", "1", "-i", "pipe:0", "-filter_complex", graph.str(),
        "-map", "[palette]", "-frames:v", "1", "-f", "rawvideo", "-pix_fmt",
        "rgba", "pipe:1"
    };
    std::vector<uint8_t> rgba = run_capture(cmd, &seed, "FFmpeg seeded palette generation");
    if (rgba.empty() || rgba.size() % 4 != 0)
        throw std::runtime_error("FFmpeg returned an invalid RGBA palette");

    std::vector<uint16_t> palette = make_seed_palette();
    std::array<uint8_t, 65536> seen{};
    for (uint16_t c : palette) seen[c] = 1;
    size_t added = 0;
    for (size_t i = 0; i + 3 < rgba.size() && palette.size() < budget; i += 4) {
        if (rgba[i+3] == 0) continue;
        uint16_t c = rgb565_round(rgba[i], rgba[i+1], rgba[i+2]);
        if (!seen[c]) {
            seen[c] = 1;
            palette.push_back(c);
            ++added;
        }
    }
    (void)added;
    if (palette.size() < 36 || palette.size() > budget)
        throw std::runtime_error("FFmpeg generated an invalid global palette");
    return palette;
}

static std::array<uint8_t, 65536> build_lookup(const std::vector<uint16_t> &palette) {
    std::array<uint8_t, 65536> lookup{};
    std::array<int32_t, 65536> best;
    best.fill(std::numeric_limits<int32_t>::max());
    for (size_t i = 0; i < palette.size(); ++i) {
        uint32_t c = palette[i];
        int pr=(c>>11)&31, pg=(c>>5)&63, pb=c&31;
        for (uint32_t v = 0; v < 65536; ++v) {
            int dr=static_cast<int>((v>>11)&31)-pr;
            int dg=static_cast<int>((v>>5)&63)-pg;
            int db=static_cast<int>(v&31)-pb;
            int32_t d=3*dr*dr+4*dg*dg+2*db*db;
            if (d < best[v]) { best[v]=d; lookup[v]=static_cast<uint8_t>(i); }
        }
    }
    return lookup;
}

static uint8_t index_bits(size_t palette_count) {
    if (palette_count <= 1) return 1;
    uint8_t bits = 0;
    size_t value = palette_count - 1;
    while (value) { ++bits; value >>= 1; }
    return bits;
}

struct MappedFrame {
    std::vector<uint8_t> indices;
    std::vector<uint16_t> reconstructed;
    std::vector<uint8_t> raw_mask;
};

static MappedFrame map_frame(
    const std::vector<uint8_t> &rgb,
    const std::vector<uint16_t> &palette,
    const std::array<uint8_t,65536> &lookup,
    bool lossless_palette
) {
    const size_t n = rgb.size() / 3;
    MappedFrame frame;
    frame.indices.resize(n);
    frame.reconstructed.resize(n);
    if (lossless_palette) frame.raw_mask.resize(n);
    for (size_t i = 0, p = 0; i < n; ++i, p += 3) {
        uint16_t source = rgb565_round(rgb[p], rgb[p+1], rgb[p+2]);
        uint8_t index = lookup[source];
        uint16_t mapped = palette[index];
        frame.indices[i] = index;
        frame.reconstructed[i] = lossless_palette ? source : mapped;
        if (lossless_palette) frame.raw_mask[i] = static_cast<uint8_t>(source != mapped);
    }
    return frame;
}

static size_t packet_header_size(size_t n) { return n <= 32 ? 1 : 2; }

static void append_packet_header(std::vector<uint8_t> &out, uint8_t opcode, size_t n) {
    if (n < 1 || n > MAX_RUN) throw std::runtime_error("invalid packet run length");
    if (n <= 32) out.push_back(static_cast<uint8_t>((opcode << 6) | (n - 1)));
    else {
        out.push_back(static_cast<uint8_t>((opcode << 6) | 0x20 | ((n >> 8) & 0x1F)));
        out.push_back(static_cast<uint8_t>(n & 0xFF));
    }
}

static void pack_indices(
    std::vector<uint8_t> &out, const std::vector<uint8_t> &values,
    size_t start, size_t count, uint8_t bits
) {
    if (bits == 8) {
        out.insert(out.end(), values.begin() + static_cast<ptrdiff_t>(start),
                   values.begin() + static_cast<ptrdiff_t>(start + count));
        return;
    }
    uint32_t accumulator = 0;
    unsigned filled = 0;
    for (size_t i = 0; i < count; ++i) {
        accumulator |= static_cast<uint32_t>(values[start+i]) << filled;
        filled += bits;
        while (filled >= 8) {
            out.push_back(static_cast<uint8_t>(accumulator & 0xFF));
            accumulator >>= 8;
            filled -= 8;
        }
    }
    if (filled) out.push_back(static_cast<uint8_t>(accumulator & 0xFF));
}

static size_t literal_cost(size_t n, uint8_t bits) {
    size_t total = 0;
    while (n) {
        size_t part = std::min<size_t>(MAX_RUN, n);
        total += packet_header_size(part) + (part * bits + 7) / 8;
        n -= part;
    }
    return total;
}

static size_t repeat_cost(size_t n) {
    size_t full = n / MAX_RUN, rem = n % MAX_RUN;
    size_t total = full * 3;
    if (rem) total += packet_header_size(rem) + 1;
    return total;
}

static void emit_literal(
    std::vector<uint8_t> &out, const std::vector<uint8_t> &values,
    size_t start, size_t count, PacketStats &stats, uint8_t bits
) {
    size_t pos = start, remaining = count;
    while (remaining) {
        size_t n = std::min<size_t>(MAX_RUN, remaining);
        size_t before = out.size();
        append_packet_header(out, OP_LITERAL, n);
        pack_indices(out, values, pos, n, bits);
        stats.add(OP_LITERAL, out.size() - before);
        pos += n; remaining -= n;
    }
}

static void emit_repeat(
    std::vector<uint8_t> &out, uint8_t value, size_t count, PacketStats &stats
) {
    while (count) {
        size_t n = std::min<size_t>(MAX_RUN, count);
        size_t before = out.size();
        append_packet_header(out, OP_REPEAT, n);
        out.push_back(value);
        stats.add(OP_REPEAT, out.size() - before);
        count -= n;
    }
}

static void emit_skip(std::vector<uint8_t> &out, size_t count, PacketStats &stats) {
    while (count) {
        size_t n = std::min<size_t>(MAX_RUN, count);
        size_t before = out.size();
        append_packet_header(out, OP_SKIP, n);
        stats.add(OP_SKIP, out.size() - before);
        count -= n;
    }
}

static void emit_raw(
    std::vector<uint8_t> &out, const std::vector<uint16_t> &values,
    size_t start, size_t count, PacketStats &stats
) {
    size_t pos = start, remaining = count;
    while (remaining) {
        size_t n = std::min<size_t>(MAX_RUN, remaining);
        size_t before = out.size();
        append_packet_header(out, OP_RAW565, n);
        for (size_t i = 0; i < n; ++i) {
            uint16_t v = values[pos+i];
            out.push_back(static_cast<uint8_t>(v));
            out.push_back(static_cast<uint8_t>(v >> 8));
        }
        stats.add(OP_RAW565, out.size() - before);
        pos += n; remaining -= n;
    }
}

struct Run { size_t start, length; uint8_t value; bool repeated; };

static void encode_palette_segment(
    std::vector<uint8_t> &out, const std::vector<uint8_t> &values,
    size_t start, size_t length, PacketStats &stats, uint8_t bits
) {
    if (!length) return;
    std::vector<Run> runs;
    size_t pos = 0;
    while (pos < length) {
        size_t end = pos + 1;
        while (end < length && values[start+end] == values[start+pos]) ++end;
        runs.push_back({pos, end-pos, values[start+pos], end-pos >= 4});
        pos = end;
    }
    size_t group_count = 0;
    bool previous_type = false;
    for (size_t i = 0; i < runs.size(); ++i) {
        if (i == 0 || runs[i].repeated != previous_type) ++group_count;
        previous_type = runs[i].repeated;
    }
    size_t all_literal = literal_cost(length, bits);
    if (group_count == 1 && !runs[0].repeated) {
        emit_literal(out, values, start, length, stats, bits);
        return;
    }
    if (group_count > 256) {
        emit_literal(out, values, start, length, stats, bits);
        return;
    }
    size_t plan_cost = 0;
    for (size_t g = 0; g < runs.size();) {
        size_t e = g + 1;
        while (e < runs.size() && runs[e].repeated == runs[g].repeated) ++e;
        if (runs[g].repeated) {
            for (size_t i = g; i < e; ++i) plan_cost += repeat_cost(runs[i].length);
        } else {
            size_t pixels = runs[e-1].start + runs[e-1].length - runs[g].start;
            plan_cost += literal_cost(pixels, bits);
        }
        g = e;
    }
    size_t threshold = std::max<size_t>(3, all_literal / 100);
    if (plan_cost + threshold >= all_literal) {
        emit_literal(out, values, start, length, stats, bits);
        return;
    }
    for (size_t g = 0; g < runs.size();) {
        size_t e = g + 1;
        while (e < runs.size() && runs[e].repeated == runs[g].repeated) ++e;
        if (runs[g].repeated) {
            for (size_t i = g; i < e; ++i) emit_repeat(out, runs[i].value, runs[i].length, stats);
        } else {
            size_t first = runs[g].start;
            size_t last = runs[e-1].start + runs[e-1].length;
            emit_literal(out, values, start + first, last - first, stats, bits);
        }
        g = e;
    }
}

static void encode_normal_delta(
    std::vector<uint8_t> &out,
    const std::vector<uint8_t> &indices,
    const std::vector<uint16_t> &reconstructed,
    const std::vector<uint16_t> &previous,
    size_t start, size_t length, PacketStats &stats, uint8_t bits
) {
    size_t pos = 0;
    while (pos < length) {
        bool changed = reconstructed[start+pos] != previous[start+pos];
        size_t end = pos + 1;
        while (end < length &&
               (reconstructed[start+end] != previous[start+end]) == changed) ++end;
        if (changed) encode_palette_segment(out, indices, start+pos, end-pos, stats, bits);
        else emit_skip(out, end-pos, stats);
        pos = end;
    }
}

static void append_packed_mask(
    std::vector<uint8_t> &out, const std::vector<uint8_t> &mask,
    size_t start, size_t length
) {
    uint8_t byte = 0;
    unsigned bit = 0;
    for (size_t i = 0; i < length; ++i) {
        if (mask[start+i]) byte |= static_cast<uint8_t>(1u << bit);
        if (++bit == 8) { out.push_back(byte); byte = 0; bit = 0; }
    }
    if (bit) out.push_back(byte);
}

static void encode_palette_delta(
    std::vector<uint8_t> &out,
    const std::vector<uint8_t> &indices,
    const std::vector<uint16_t> &reconstructed,
    const std::vector<uint16_t> &previous,
    size_t start, size_t length, PacketStats &stats,
    uint8_t bits, bool allow_mask
) {
    for (size_t chunk = 0; chunk < length;) {
        size_t n = std::min<size_t>(MAX_MASK_SPAN, length - chunk);
        size_t absolute = start + chunk;
        std::vector<uint8_t> changed(n);
        size_t changed_count = 0;
        for (size_t i = 0; i < n; ++i) {
            changed[i] = static_cast<uint8_t>(reconstructed[absolute+i] != previous[absolute+i]);
            changed_count += changed[i];
        }
        std::vector<uint8_t> normal;
        PacketStats normal_stats;
        encode_normal_delta(normal, indices, reconstructed, previous,
                            absolute, n, normal_stats, bits);
        if (allow_mask && changed_count > 0 && changed_count < n) {
            std::vector<uint8_t> candidate;
            candidate.reserve(4 + (n+7)/8 + (changed_count*bits+7)/8);
            candidate.push_back(0xA0);
            candidate.push_back(0x00);
            candidate.push_back(static_cast<uint8_t>(n));
            candidate.push_back(static_cast<uint8_t>(n >> 8));
            append_packed_mask(candidate, changed, 0, n);
            std::vector<uint8_t> changed_indices;
            changed_indices.reserve(changed_count);
            for (size_t i = 0; i < n; ++i)
                if (changed[i]) changed_indices.push_back(indices[absolute+i]);
            pack_indices(candidate, changed_indices, 0, changed_indices.size(), bits);
            if (candidate.size() < normal.size()) {
                stats.add(OP_MASK, candidate.size());
                out.insert(out.end(), candidate.begin(), candidate.end());
            } else {
                stats.merge(normal_stats);
                out.insert(out.end(), normal.begin(), normal.end());
            }
        } else {
            stats.merge(normal_stats);
            out.insert(out.end(), normal.begin(), normal.end());
        }
        chunk += n;
    }
}

struct EncodedFrame {
    std::vector<uint8_t> payload;
    PacketStats stats;
};

static EncodedFrame encode_frame(
    const MappedFrame &frame,
    const std::vector<uint16_t> *previous,
    uint8_t frame_type,
    uint8_t bits,
    bool allow_mask
) {
    EncodedFrame result;
    const size_t count = frame.reconstructed.size();
    std::vector<uint8_t> raw_positions(count, 0);
    if (frame_type == FRAME_I) {
        if (!frame.raw_mask.empty()) raw_positions = frame.raw_mask;
    } else {
        for (size_t i = 0; i < count; ++i)
            raw_positions[i] = (!frame.raw_mask.empty() && frame.raw_mask[i] &&
                                frame.reconstructed[i] != (*previous)[i]);
    }
    size_t pos = 0;
    while (pos < count) {
        bool raw = raw_positions[pos] != 0;
        size_t end = pos + 1;
        while (end < count && (raw_positions[end] != 0) == raw) ++end;
        if (raw) {
            emit_raw(result.payload, frame.reconstructed, pos, end-pos, result.stats);
        } else if (frame_type == FRAME_P) {
            encode_palette_delta(result.payload, frame.indices, frame.reconstructed,
                *previous, pos, end-pos, result.stats, bits, allow_mask);
        } else {
            encode_palette_segment(result.payload, frame.indices, pos, end-pos,
                                   result.stats, bits);
        }
        pos = end;
    }
    return result;
}

struct CompressionResult {
    std::vector<uint8_t> bytes;
    uint8_t flag = 0;
    uint64_t saved = 0;
};

static void append_u32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value >> 16));
    out.push_back(static_cast<uint8_t>(value >> 24));
}

static CompressionResult compress_payload(const std::vector<uint8_t> &payload, bool enabled) {
    CompressionResult result;
    result.bytes = payload;
    if (!enabled || payload.empty()) return result;
    if (payload.size() > std::numeric_limits<uLong>::max())
        throw std::runtime_error("frame payload is too large for zlib");
    uLongf deflate_bound = compressBound(static_cast<uLong>(payload.size()));
    std::vector<uint8_t> deflate_data(deflate_bound);
    int zret = compress2(deflate_data.data(), &deflate_bound, payload.data(),
                         static_cast<uLong>(payload.size()), Z_BEST_COMPRESSION);
    if (zret != Z_OK) throw std::runtime_error("zlib DEFLATE compression failed");
    deflate_data.resize(deflate_bound);
    std::vector<uint8_t> best;
    best.reserve(4 + deflate_data.size());
    append_u32(best, static_cast<uint32_t>(payload.size()));
    best.insert(best.end(), deflate_data.begin(), deflate_data.end());
    uint8_t best_flag = FLAG_DEFLATE;

    if (payload.size() <= static_cast<size_t>(std::numeric_limits<int>::max())) {
        int bound = LZ4_compressBound(static_cast<int>(payload.size()));
        if (bound > 0) {
            std::vector<char> lz(bound);
            int compressed = LZ4_compress_HC(
                reinterpret_cast<const char *>(payload.data()), lz.data(),
                static_cast<int>(payload.size()), bound, 9
            );
            if (compressed > 0 && static_cast<size_t>(compressed) + 4 < best.size()) {
                best.clear();
                append_u32(best, static_cast<uint32_t>(payload.size()));
                best.insert(best.end(), reinterpret_cast<uint8_t *>(lz.data()),
                            reinterpret_cast<uint8_t *>(lz.data()) + compressed);
                best_flag = FLAG_LZ4;
            }
        }
    }
    uint64_t saving = payload.size() > best.size() ? payload.size() - best.size() : 0;
    if (saving < std::max<uint64_t>(16, payload.size()/100)) return result;
    result.bytes = std::move(best);
    result.flag = best_flag;
    result.saved = saving;
    return result;
}

static size_t motion_matches(
    const std::vector<uint16_t> &current,
    const std::vector<uint16_t> &previous,
    int width, int height, int dx, int dy, int step
) {
    int sx0 = std::max(0, -dx), sy0 = std::max(0, -dy);
    int sx1 = std::min(width, width-dx), sy1 = std::min(height, height-dy);
    size_t matches = 0;
    for (int y = sy0; y < sy1; y += step) {
        for (int x = sx0; x < sx1; x += step) {
            if (previous[static_cast<size_t>(y)*width+x] ==
                current[static_cast<size_t>(y+dy)*width+(x+dx)]) ++matches;
        }
    }
    return matches;
}

static std::pair<int,int> estimate_motion(
    const std::vector<uint16_t> &current,
    const std::vector<uint16_t> &previous,
    int width, int height
) {
    int coarse_step = std::max(8, std::max(width,height)/32);
    int max_dx = std::min(8, width-1), max_dy = std::min(8, height-1);
    int coarse_dx=0, coarse_dy=0;
    size_t coarse_score = motion_matches(current, previous, width,height,0,0,coarse_step);
    for (int dy=-max_dy; dy<=max_dy; ++dy) {
        for (int dx=-max_dx; dx<=max_dx; ++dx) {
            if (!dx && !dy) continue;
            size_t score = motion_matches(current,previous,width,height,dx,dy,coarse_step);
            if (score > coarse_score) { coarse_score=score; coarse_dx=dx; coarse_dy=dy; }
        }
    }
    int fine_step = std::max(2, coarse_step/2);
    size_t zero_score = motion_matches(current,previous,width,height,0,0,fine_step);
    size_t best_score = zero_score;
    int best_dx=0, best_dy=0;
    for (int dy=std::max(-max_dy,coarse_dy-1); dy<=std::min(max_dy,coarse_dy+1); ++dy) {
        for (int dx=std::max(-max_dx,coarse_dx-1); dx<=std::min(max_dx,coarse_dx+1); ++dx) {
            size_t score = motion_matches(current,previous,width,height,dx,dy,fine_step);
            if (score > best_score ||
                (score == best_score && std::abs(dx)+std::abs(dy) <
                                        std::abs(best_dx)+std::abs(best_dy))) {
                best_score=score; best_dx=dx; best_dy=dy;
            }
        }
    }
    size_t samples = ((height+fine_step-1)/fine_step) * ((width+fine_step-1)/fine_step);
    if ((!best_dx && !best_dy) ||
        best_score-zero_score < std::max<size_t>(3, samples/100)) return {0,0};
    return {best_dx,best_dy};
}

static std::vector<uint16_t> shift_frame(
    const std::vector<uint16_t> &previous, int width, int height,
    int dx, int dy, uint16_t fill
) {
    std::vector<uint16_t> shifted(static_cast<size_t>(width)*height, fill);
    for (int y = 0; y < height; ++y) {
        int sy = y - dy;
        if (sy < 0 || sy >= height) continue;
        int x0 = std::max(0, dx), x1 = std::min(width, width+dx);
        for (int x = x0; x < x1; ++x) {
            int sx = x-dx;
            shifted[static_cast<size_t>(y)*width+x] =
                previous[static_cast<size_t>(sy)*width+sx];
        }
    }
    return shifted;
}

static uint32_t timestamp_ms(uint64_t frame, uint32_t fps_num, uint32_t fps_den) {
    uint64_t numerator = frame * 1000ULL * fps_den;
    uint64_t rounded = (numerator + fps_num/2) / fps_num;
    if (rounded > 0xFFFFFFFFULL) throw std::runtime_error("video duration exceeds CPV uint32 range");
    return static_cast<uint32_t>(rounded);
}

static uint32_t probe_duration_ms(const Options &o, const std::string &source) {
    const std::vector<std::vector<std::string>> commands = {
        {o.ffprobe,"-v","error","-select_streams","v:0","-show_entries","stream=duration",
         "-of","default=noprint_wrappers=1:nokey=1",source},
        {o.ffprobe,"-v","error","-show_entries","format=duration",
         "-of","default=noprint_wrappers=1:nokey=1",source}
    };
    for (const auto &cmd : commands) {
        try {
            auto data = run_capture(cmd, nullptr, "ffprobe");
            std::string s(data.begin(), data.end());
            std::istringstream in(s);
            std::string line;
            if (std::getline(in,line)) {
                char *end=nullptr;
                double seconds=strtod(line.c_str(),&end);
                if (end && end != line.c_str() && std::isfinite(seconds) && seconds>0) {
                    double ms=std::round(seconds*1000.0);
                    if (ms >= 1 && ms <= 4294967295.0) return static_cast<uint32_t>(ms);
                }
            }
        } catch (...) {}
    }
    return 0;
}

static void write_bytes(FILE *file, const void *data, size_t size) {
    if (size && fwrite(data,1,size,file) != size)
        throw std::runtime_error("failed writing output file");
}
static void write_u16le(FILE *file, uint16_t v) {
    uint8_t b[2]={static_cast<uint8_t>(v),static_cast<uint8_t>(v>>8)};
    write_bytes(file,b,2);
}
static void write_u32le(FILE *file, uint32_t v) {
    uint8_t b[4]={static_cast<uint8_t>(v),static_cast<uint8_t>(v>>8),
                  static_cast<uint8_t>(v>>16),static_cast<uint8_t>(v>>24)};
    write_bytes(file,b,4);
}
static uint64_t file_offset(FILE *file) {
    off_t p=ftello(file);
    if (p<0) throw std::runtime_error("could not determine output file offset");
    return static_cast<uint64_t>(p);
}
static void patch_u32(FILE *file, uint64_t offset, uint32_t value) {
    uint64_t end=file_offset(file);
    if (fseeko(file,static_cast<off_t>(offset),SEEK_SET)!=0)
        throw std::runtime_error("could not seek to frame duration field");
    write_u32le(file,value);
    if (fseeko(file,static_cast<off_t>(end),SEEK_SET)!=0)
        throw std::runtime_error("could not restore output file position");
}
static uint64_t write_frame_record(
    FILE *file, const std::vector<uint8_t> &payload,
    uint8_t type, uint8_t flags, uint32_t number, uint32_t duration
) {
    if (!duration || payload.size()>0xFFFFFFFFULL) throw std::runtime_error("invalid frame size or duration");
    if ((flags & ~(FLAG_LZ4|FLAG_DEFLATE|FLAG_MOTION)) ||
        (flags & (FLAG_LZ4|FLAG_DEFLATE))==(FLAG_LZ4|FLAG_DEFLATE))
        throw std::runtime_error("invalid frame flags");
    uint64_t offset=file_offset(file);
    if (offset>0xFFFFFFFFULL || payload.size()+20 > 0xFFFFFFFFULL-offset)
        throw std::runtime_error("output exceeds CPV v1 4 GiB limit");
    write_bytes(file,"FRM1",4);
    uint8_t fields[4]={type,flags,20,0};
    write_bytes(file,fields,4);
    write_u32le(file,number);
    write_u32le(file,duration);
    write_u32le(file,static_cast<uint32_t>(payload.size()));
    write_bytes(file,payload.data(),payload.size());
    return offset;
}

static void write_file_header(
    FILE *file,const Options &o,uint32_t frame_count,uint16_t palette_count,
    uint32_t frames_offset,uint32_t index_offset,uint32_t duration
) {
    std::vector<uint8_t> h(64,0);
    h[0]='C'; h[1]='P'; h[2]='V'; h[3]=0;
    h[4]=1; h[5]=3;
    auto u16=[&](size_t p,uint16_t v){ h[p]=static_cast<uint8_t>(v); h[p+1]=static_cast<uint8_t>(v>>8); };
    auto u32=[&](size_t p,uint32_t v){ for(unsigned i=0;i<4;++i) h[p+i]=static_cast<uint8_t>(v>>(i*8)); };
    u16(6,64); u32(8,0);
    u16(12,static_cast<uint16_t>(o.width));
    u16(14,static_cast<uint16_t>(o.height));
    u16(16,static_cast<uint16_t>(o.fps_num));
    u16(18,static_cast<uint16_t>(o.fps_den));
    u32(20,frame_count); u16(24,palette_count); u16(26,0);
    u32(28,64); u32(32,frames_offset); u32(36,index_offset); u32(40,duration);
    if (fseeko(file,0,SEEK_SET)!=0) throw std::runtime_error("could not seek to file header");
    write_bytes(file,h.data(),h.size());
}

static void run_self_tests() {
    if (make_seed_rgb24().size()!=108 || make_seed_palette().size()!=36)
        throw std::runtime_error("seed palette self-test failed");
    if (index_bits(1)!=1 || index_bits(16)!=4 || index_bits(17)!=5 ||
        index_bits(64)!=6 || index_bits(128)!=7 || index_bits(256)!=8)
        throw std::runtime_error("palette index width self-test failed");
    std::vector<uint8_t> values={0,1,2,3,4,5,6,7,8,9,10};
    std::vector<uint8_t> packed;
    pack_indices(packed,values,0,values.size(),3);
    std::vector<uint8_t> decoded;
    uint32_t acc=0; unsigned have=0;
    for (uint8_t b:packed) {
        acc |= static_cast<uint32_t>(b)<<have; have+=8;
        while(have>=3 && decoded.size()<values.size()) {
            decoded.push_back(static_cast<uint8_t>(acc&7)); acc>>=3; have-=3;
        }
    }
    if (decoded!=values) throw std::runtime_error("bit-packing self-test failed");
    std::vector<uint16_t> prev(20*16);
    for(size_t i=0;i<prev.size();++i) prev[i]=static_cast<uint16_t>(i*37+11);
    auto moved=shift_frame(prev,20,16,3,-2,0x1234);
    for(int y=0;y<16;++y) for(int x=0;x<20;++x) {
        int sx=x-3, sy=y+2;
        uint16_t expected=(sx>=0&&sx<20&&sy>=0&&sy<16)
            ? prev[static_cast<size_t>(sy)*20+sx] : 0x1234;
        if(moved[static_cast<size_t>(y)*20+x]!=expected)
            throw std::runtime_error("global-motion shift self-test failed");
    }
    if (estimate_motion(moved,prev,20,16)!=std::make_pair(3,-2))
        throw std::runtime_error("global-motion estimator self-test failed");
    std::vector<uint8_t> literal;
    PacketStats stats;
    emit_literal(literal,values,0,values.size(),stats,4);
    if(literal.empty() || stats.count[OP_LITERAL]!=1)
        throw std::runtime_error("literal packet self-test failed");
    std::cout << "CPV native encoder self-tests: OK\n";
}

static void convert_video(const Options &o) {
    fs::path input=fs::absolute(o.input).lexically_normal();
    fs::path output=fs::absolute(o.output).lexically_normal();
    if (!fs::is_regular_file(input)) throw std::runtime_error("input file does not exist: "+input.string());
    if (input==output) throw std::runtime_error("input and output paths must be different");
    fs::create_directories(output.parent_path());
    uint32_t source_duration=probe_duration_ms(o,input.string());
    if (!source_duration)
        std::cerr << "Warning: ffprobe could not determine source duration; final frame uses nominal duration.\n";

    auto started=std::chrono::steady_clock::now();
    std::vector<uint16_t> palette=build_palette(o,input.string());
    auto lookup=build_lookup(palette);
    uint32_t budget=palette_budget_for(o.palette_quality);
    std::cout << "Global palette: " << palette.size() << "/" << budget
              << " colours (palette quality " << o.palette_quality
              << "/100; pre-process quality " << o.preprocess_quality << "/100)\n";

    std::string filter=make_filter(o,o.preprocess_quality);
    std::vector<std::string> ffmpeg_cmd={
        o.ffmpeg,"-hide_banner","-loglevel","error","-i",input.string(),
        "-map","0:v:0","-vf",filter,"-an","-sn","-dn",
        "-f","rawvideo","-pix_fmt","rgb24","pipe:1"
    };
    ChildProcess process=launch_process(ffmpeg_cmd,false);
    fs::path temp=output;
    temp += ".tmp."+std::to_string(static_cast<long long>(getpid()));
    FILE *file=nullptr;
    bool committed=false;
    try {
        file=fopen(temp.c_str(),"wb+");
        if(!file) throw std::runtime_error("could not create temporary output: "+temp.string());
        std::array<uint8_t,64> empty_header{};
        write_bytes(file,empty_header.data(),empty_header.size());
        for(uint16_t c:palette) write_u16le(file,c);
        uint64_t frames_offset64=file_offset(file);
        if(frames_offset64>0xFFFFFFFFULL) throw std::runtime_error("output exceeds CPV 4 GiB limit");
        uint32_t frames_offset=static_cast<uint32_t>(frames_offset64);

        const size_t pixels=static_cast<size_t>(o.width)*o.height;
        std::vector<uint8_t> raw(pixels*3);
        std::vector<uint16_t> previous;
        uint32_t input_count=0,stored_count=0,total_duration=0;
        uint32_t last_input_start=0,last_input_duration=0,last_frame_duration=0;
        uint64_t last_frame_offset=0;
        uint32_t last_keyframe_start=0;
        bool have_previous=false,have_keyframe=false,have_last_frame=false;
        std::vector<std::array<uint32_t,3>> keyframes;
        PacketStats total_stats;
        uint64_t lz4_frames=0,deflate_frames=0,compression_saved=0;
        uint64_t motion_frames=0,motion_saved=0;
        const uint32_t key_interval=std::max<uint32_t>(1,static_cast<uint32_t>(std::llround(o.keyframe_interval*1000.0)));
        const uint16_t fill=rgb565_round(
            static_cast<uint8_t>(o.background>>16),
            static_cast<uint8_t>(o.background>>8),
            static_cast<uint8_t>(o.background)
        );
        const uint8_t bits=index_bits(palette.size());

        while(read_exact_frame(process.out_fd,raw)) {
            uint32_t start=timestamp_ms(input_count,o.fps_num,o.fps_den);
            uint32_t next=timestamp_ms(static_cast<uint64_t>(input_count)+1,o.fps_num,o.fps_den);
            uint32_t duration=std::max<uint32_t>(1,next-start);
            last_input_start=start;
            last_input_duration=duration;

            MappedFrame mapped=map_frame(raw,palette,lookup,o.palette_quality==100);
            bool identical=have_previous &&
                std::equal(mapped.reconstructed.begin(),mapped.reconstructed.end(),previous.begin());
            if(identical) {
                if(!have_last_frame) throw std::runtime_error("internal error: previous frame record missing");
                uint64_t d=static_cast<uint64_t>(last_frame_duration)+duration;
                if(d>0xFFFFFFFFULL) throw std::runtime_error("coalesced frame duration exceeds uint32");
                last_frame_duration=static_cast<uint32_t>(d);
                patch_u32(file,last_frame_offset+12,last_frame_duration);
            } else {
                bool keyframe=!have_previous || !have_keyframe || start-last_keyframe_start>=key_interval;
                uint8_t type=keyframe?FRAME_I:FRAME_P;
                EncodedFrame encoded=encode_frame(mapped,have_previous?&previous:nullptr,
                    type,bits,!o.no_delta_mask);
                CompressionResult compressed=compress_payload(encoded.payload,!o.no_compression);
                std::vector<uint8_t> stored=std::move(compressed.bytes);
                uint8_t flags=compressed.flag;
                uint64_t frame_saving=compressed.saved;

                if(type==FRAME_P && !o.no_motion) {
                    auto motion=estimate_motion(mapped.reconstructed,previous,
                        static_cast<int>(o.width),static_cast<int>(o.height));
                    int dx=motion.first,dy=motion.second;
                    if(dx||dy) {
                        std::vector<uint16_t> predicted=shift_frame(previous,o.width,o.height,dx,dy,fill);
                        EncodedFrame motion_packets=encode_frame(mapped,&predicted,
                            FRAME_P,bits,!o.no_delta_mask);
                        std::vector<uint8_t> motion_stream;
                        motion_stream.reserve(4+motion_packets.payload.size());
                        motion_stream.push_back(static_cast<uint8_t>(static_cast<int8_t>(dx)));
                        motion_stream.push_back(static_cast<uint8_t>(static_cast<int8_t>(dy)));
                        motion_stream.push_back(static_cast<uint8_t>(fill));
                        motion_stream.push_back(static_cast<uint8_t>(fill>>8));
                        motion_stream.insert(motion_stream.end(),motion_packets.payload.begin(),motion_packets.payload.end());
                        CompressionResult motion_compressed=compress_payload(motion_stream,!o.no_compression);
                        if(motion_compressed.bytes.size()<stored.size()) {
                            motion_saved+=stored.size()-motion_compressed.bytes.size();
                            ++motion_frames;
                            stored=std::move(motion_compressed.bytes);
                            flags=static_cast<uint8_t>(motion_compressed.flag|FLAG_MOTION);
                            frame_saving=motion_compressed.saved;
                            compressed.flag=motion_compressed.flag;
                            encoded.stats=std::move(motion_packets.stats);
                        }
                    }
                }
                if(compressed.flag==FLAG_LZ4) ++lz4_frames;
                else if(compressed.flag==FLAG_DEFLATE) ++deflate_frames;
                compression_saved+=frame_saving;

                uint64_t offset=write_frame_record(file,stored,type,flags,stored_count,duration);
                if(type==FRAME_I) {
                    keyframes.push_back({stored_count,start,static_cast<uint32_t>(offset)});
                    last_keyframe_start=start;
                    have_keyframe=true;
                }
                total_stats.merge(encoded.stats);
                ++stored_count;
                last_frame_offset=offset;
                last_frame_duration=duration;
                have_last_frame=true;
                previous=std::move(mapped.reconstructed);
                have_previous=true;
            }
            uint64_t td=static_cast<uint64_t>(total_duration)+duration;
            if(td>0xFFFFFFFFULL) throw std::runtime_error("video duration exceeds CPV uint32 limit");
            total_duration=static_cast<uint32_t>(td);
            ++input_count;
            if(input_count%50==0)
                std::cerr << "\rRead " << input_count << " frames; stored " << stored_count << std::flush;
        }

        close_fd(process.out_fd);
        std::string ffmpeg_error;
        int code=finish_process(process,ffmpeg_error);
        if(code!=0)
            throw std::runtime_error("FFmpeg video decoding failed (exit "+std::to_string(code)+")"+
                (ffmpeg_error.empty()?"":": "+ffmpeg_error));
        if(!input_count || !stored_count || !have_last_frame)
            throw std::runtime_error("FFmpeg produced no video frames");

        uint32_t nominal_end=timestamp_ms(input_count,o.fps_num,o.fps_den);
        uint32_t desired_end=(source_duration>last_input_start)?source_duration:nominal_end;
        uint32_t final_duration=std::max<uint32_t>(1,desired_end-last_input_start);
        int64_t adjust=static_cast<int64_t>(final_duration)-last_input_duration;
        if(adjust) {
            int64_t patched=static_cast<int64_t>(last_frame_duration)+adjust;
            int64_t total=static_cast<int64_t>(total_duration)+adjust;
            if(patched<=0||patched>0xFFFFFFFFLL||total<=0||total>0xFFFFFFFFLL)
                throw std::runtime_error("final frame duration is outside CPV uint32 range");
            last_frame_duration=static_cast<uint32_t>(patched);
            total_duration=static_cast<uint32_t>(total);
            patch_u32(file,last_frame_offset+12,last_frame_duration);
        }

        uint64_t index_offset64=file_offset(file);
        if(index_offset64>0xFFFFFFFFULL || keyframes.size()>0xFFFFFFFFULL)
            throw std::runtime_error("output exceeds CPV index limits");
        write_bytes(file,"IDX1",4);
        write_u32le(file,static_cast<uint32_t>(keyframes.size()));
        for(const auto &entry:keyframes) {
            write_u32le(file,entry[0]); write_u32le(file,entry[1]); write_u32le(file,entry[2]);
        }
        uint64_t end_offset=file_offset(file);
        if(end_offset>0xFFFFFFFFULL) throw std::runtime_error("output exceeds CPV v1 4 GiB limit");
        write_file_header(file,o,stored_count,static_cast<uint16_t>(palette.size()),
            frames_offset,static_cast<uint32_t>(index_offset64),total_duration);
        if(fflush(file)!=0 || fsync(fileno(file))!=0)
            throw std::runtime_error("could not flush output file");
        if(fclose(file)!=0) { file=nullptr; throw std::runtime_error("could not close output file"); }
        file=nullptr;
        if(rename(temp.c_str(),output.c_str())!=0)
            throw std::runtime_error("could not atomically replace output file: "+std::string(strerror(errno)));
        committed=true;

        double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
        uint64_t size=fs::file_size(output);
        if(input_count%50) std::cerr << "\n";
        std::cout << "Created " << output.string() << "\n"
            << "  Resolution: " << o.width << "x" << o.height << "\n"
            << "  Frame rate: " << o.fps_num << "/" << o.fps_den << " FPS\n"
            << "  Input frames: " << input_count << "\n"
            << "  Stored frames: " << stored_count << "\n"
            << "  Keyframes: " << keyframes.size() << "\n"
            << "  Duration: " << total_duration/1000.0 << " s\n"
            << "  Palette: " << palette.size() << " colours (36 legacy seed colours + adaptive video colours)\n"
            << "  Palette quality: " << o.palette_quality << "/100\n"
            << "  Pre-process quality: " << o.preprocess_quality << "/100\n"
            << "  Palette index width: " << static_cast<unsigned>(bits) << " bits\n"
            << "  LZ4-HC frames: " << lz4_frames << "/" << stored_count << "\n"
            << "  DEFLATE/Huffman frames: " << deflate_frames << "/" << stored_count << "\n"
            << "  Compression bytes saved: " << compression_saved << "\n"
            << "  Global-motion frames: " << motion_frames << "/" << stored_count << "\n"
            << "  Bytes saved by global motion: " << motion_saved << "\n"
            << "  File size: " << size << " bytes (" << size/(1024.0*1024.0) << " MiB)\n"
            << "  Conversion time: " << elapsed << " s\n"
            << "  Conversion speed: " << (total_duration/1000.0)/std::max(0.001,elapsed) << "x realtime\n"
            << "  Packets: ";
        for(size_t i=0;i<OP_NAMES.size();++i) {
            if(i) std::cout << ", ";
            std::cout << OP_NAMES[i] << "=" << total_stats.count[i]
                      << " (" << total_stats.bytes[i] << " bytes)";
        }
        std::cout << "\n";
    } catch (...) {
        abort_process(process);
        if(file) fclose(file);
        if(!committed) { std::error_code ec; fs::remove(temp,ec); }
        throw;
    }
}

int main(int argc,char **argv) {
    try {
        Options options=parse_args(argc,argv);
        if(options.input=="__CPV_SELF_TEST__") run_self_tests();
        else convert_video(options);
        return 0;
    } catch(const std::exception &e) {
        std::cerr << "cpv_convert: error: " << e.what() << "\n";
        return 1;
    }
}
