/*
 * Copyright 2014-2017 Frank Hunleth
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sodium.h>

#include "../3rdparty/base64.h"
#include "mmc.h"
#include "util.h"
#include "fwup_apply.h"
#include "fwup_create.h"
#include "fwup_list.h"
#include "fwup_metadata.h"
#include "fwup_genkeys.h"
#include "fwup_sign.h"
#include "fwup_verify.h"
#include "progress.h"
#include "simple_string.h"
#include "sparse_file.h"
#include "../config.h"

// Global options
bool fwup_verbose = false;
bool fwup_framing = false;
bool fwup_unsafe = false;
enum fwup_progress_option fwup_progress_mode = PROGRESS_MODE_OFF;

static bool quiet = false;

static void print_usage()
{
#if defined(__APPLE__)
    const char *example_sd = "/dev/rdisk2";
#elif defined(__linux__)
    const char *example_sd = "/dev/sdc";
#elif defined(__FreeBSD__)  || defined(__OpenBSD__) || defined(__DragonFly__)
    const char *example_sd = "/dev/da0";
#elif defined(__NetBSD__)
    const char *example_sd = "/dev/rsc0d";
#elif defined(_WIN32) || defined(__CYGWIN__)
    const char *example_sd = "\\\\.\\PhysicalDrive2";
#else
#error Fill in with an example SDCard/MMC device.
#endif
    const char *program_name = PACKAGE_NAME;

    printf("%s is a self-contained utility for creating and applying firmware update files.\n", program_name);
    printf("Firmware update (.fw) files are nothing more than zip archives containing metadata,\n");
    printf("a limited set of instructions, and data. On an embedded device or on a host PC, fwup\n");
    printf("is used to write nonvolatile memory (eMMC, SDCards, etc.) in such a way as to upgrade\n");
    printf("the firmware on the device or to completely initialize it.\n");
    printf("\n");
    printf("Usage: %s [OPTION]...\n", program_name);
    printf("\n");
    printf("Options:\n");
    printf("  -a, --apply   Apply the firmware update\n");
    printf("  -c, --create  Create the firmware update\n");
    printf("  -d <file> Device file for the memory card\n");
    printf("  -D, --detect List attached SDCards or MMC devices and their sizes\n");
    printf("  -E, --eject Eject removeable media after successfully writing firmware.\n");
    printf("  --no-eject Do not eject media after writing firmware\n");
    printf("  --enable-trim Enable use of the hardware TRIM command\n");
    printf("  --exit-handshake Send a Ctrl+Z on exit and wait for stdin to close (Erlang)\n");
    printf("  -f <fwupdate.conf> Specify the firmware update configuration file\n");
    printf("  -F, --framing Apply framing on stdin/stdout\n");
    printf("  -g, --gen-keys Generate firmware signing keys (fwup-key.pub and fwup-key.priv)\n");
    printf("  -i <input.fw> Specify the input firmware update file (Use - for stdin)\n");
    printf("  -l, --list   List the available tasks in a firmware update\n");
    printf("  -m, --metadata   Print metadata in the firmware update\n");
    printf("  -n   Report numeric progress\n");
    printf("  -o <output.fw> Specify the output file when creating an update (Use - for stdout)\n");
    printf("  -p, --public-key-file <keyfile> A public key file for verifying firmware updates (can specify multiple times)\n");
    printf("  --private-key <key> A private key for signing firmware updates\n");
    printf("  --progress-low <number> When displaying progress, this is the lowest number (normally 0 for 0%%)\n");
    printf("  --progress-high <number> When displaying progress, this is the highest number (normally 100 for 100%%)\n");
    printf("  --public-key <key> A public key for verifying firmware updates (can specify multiple times)\n");
    printf("  -q, --quiet   Quiet\n");
    printf("  -s, --private-key-file <keyfile> A private key file for signing firmware updates\n");
    printf("  -S, --sign Sign an existing firmware file (specify -i and -o)\n");
    printf("  --sparse-check <path> Check if the OS and file system supports sparse files at path\n");
    printf("  --sparse-check-size <bytes> Hole size to check for --sparse-check\n");
    printf("  -t, --task <task> Task to apply within the firmware update\n");
    printf("  -u, --unmount Unmount all partitions on device first\n");
    printf("  -U, --no-unmount Do not try to unmount partitions on device\n");
    printf("  --unsafe Allow unsafe commands (consider applying only signed archives)\n");
    printf("  -v, --verbose   Verbose\n");
    printf("  -V, --verify  Verify an existing firmware file (specify -i)\n");
    printf("  --version Print out the version\n");
    printf("  -y   Accept automatically found memory card when applying a firmware update\n");
    printf("  -z   Print the memory card that would be automatically detected and exit\n");
    printf("  -1   Fast compression (for create)\n");
    printf("  -9   Best compression (default)\n");
    printf("\n");
    printf("Examples:\n");
    printf("\n");
    printf("Initialize an attached SDCard using all of the default options:\n");
    printf("\n");
    printf("  $ %s myfirmware.fw\n", program_name);
    printf("\n");
    printf("Create a firmware update archive:\n");
    printf("\n");
    printf("  $ %s -c -f fwupdate.conf -o myfirmware.fw\n", program_name);
    printf("\n");
    printf("Apply the firmware to an attached SDCard. This would normally be run on the host\n");
    printf("where it would auto-detect an SDCard and initalize it using the 'complete' task:\n");
    printf("\n");
    printf("  $ %s -a -i myfirmware.fw -t complete\n", program_name);
    printf("\n");
    printf("Apply the firmware update to %s and specify the 'upgrade' task:\n", example_sd);
    printf("\n");
    printf("  $ %s -a -d %s -i myfirmware.fw -t upgrade\n", program_name, example_sd);
    printf("\n");
    printf("Create an image file from a .fw file for use with dd(1):\n");
    printf("\n");
    printf("  $ %s -a -d myimage.img -i myfirmware.fw -t complete\n", program_name);
    printf("\n");
    printf("Generate a public/private key pair:\n");
    printf("\n");
    printf("  $ %s -g\n", program_name);
    printf("\n");
    printf("Store fwup-key.priv in a safe place and fwup-key.pub on the target. To sign\n");
    printf("an existing archive run:\n");
    printf("\n");
    printf("  $ %s -S -s fwup-key.priv -i myfirmware.fw -o signedfirmware.fw\n", program_name);
    printf("\n");
    printf("Also see the unit tests that come with fwup source code for more examples.\n");
    printf("Obtain source code and report bugs at https://github.com/fhunleth/fwup.\n");
}

static void print_version()
{
    printf("%s\n", PACKAGE_VERSION);
}

static struct option long_options[] = {
    {"apply",    no_argument,       0, 'a'},
    {"create",   no_argument,       0, 'c'},
    {"detect",   no_argument,       0, 'D'},
    {"eject",    no_argument,       0, 'E'},
    {"no-eject", no_argument,       0, '#'},
    {"enable-trim", no_argument,    0, '!'},
    {"exit-handshake", no_argument, 0, '~'},
    {"framing",  no_argument,       0, 'F'},
    {"gen-keys", no_argument,       0, 'g'},
    {"help",     no_argument,       0, 'h'},
    {"list",     no_argument,       0, 'l'},
    {"metadata", no_argument,       0, 'm'},
    {"private-key", required_argument, 0, '('},
    {"private-key-file", required_argument, 0, 's'},
    {"public-key", required_argument, 0, ')'},
    {"public-key-file ", required_argument, 0, 'p'},
    {"progress-low", required_argument, 0, '$'},
    {"progress-high", required_argument, 0, '%'},
    {"quiet",    no_argument,       0, 'q'},
    {"sparse-check", required_argument, 0, '&'},
    {"sparse-check-size", required_argument, 0, '*'},
    {"sign",     no_argument,       0, 'S'},
    {"task",     required_argument, 0, 't'},
    {"unmount",  no_argument,       0, 'u'},
    {"no-unmount", no_argument,     0, 'U'},
    {"unsafe",   no_argument,       0, '+'},
    {"verbose",  no_argument,       0, 'v'},
    {"verify",   no_argument,       0, 'V'},
    {"version",  no_argument,       0, '@'},
    {0,          0,                 0, 0 }
};

#define CMD_NONE          0
#define CMD_APPLY         1
#define CMD_CREATE        2
#define CMD_LIST          3
#define CMD_METADATA      4
#define CMD_GENERATE_KEYS 5
#define CMD_SIGN          6
#define CMD_VERIFY        7
#define CMD_SPARSE_CHECK  8

static unsigned char *decode_key(const char *buffer,
                                 size_t buffer_len,
                                 size_t key_len)
{
    unsigned char *key = (unsigned char *) malloc(key_len);

    // Check for raw key bytes
    if (buffer_len == key_len) {
        memcpy(key, buffer, buffer_len);
        return key;
    }

    // Check for Base64-encoded key (with or without padding)
    size_t base64_key_len = base64_raw_to_unpadded_count(key_len);
    size_t decoded_len = key_len;
    if (buffer_len >= base64_key_len &&
        from_base64(key, &decoded_len, buffer) != NULL &&
        decoded_len == key_len) {
        return key;
    }

    // Unexpected length
    free(key);
    return NULL;
}

static unsigned char *load_key(const char *path, const char *key_type, size_t key_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        fwup_err(EXIT_FAILURE, "Error opening %s key file '%s'", key_type, path);

    size_t base64_size = base64_raw_to_encoded_count(key_len);
    char buffer[base64_size + 1];

    size_t amount_read = fread(buffer, 1, base64_size, fp);
    buffer[amount_read] = 0;
    fclose(fp);

    unsigned char *key = decode_key(buffer, amount_read, key_len);
    if (key == NULL)
        fwup_errx(EXIT_FAILURE, "Error reading or decoding %s key from file '%s'", key_type, path);

    return key;
}

static unsigned char *parse_key(const char *buffer, size_t buffer_len, const char *key_type, size_t key_len)
{
    unsigned char *key = decode_key(buffer, buffer_len, key_len);
    if (key == NULL)
        fwup_errx(EXIT_FAILURE, "Error decoding %s key", key_type);
    return key;
}

static unsigned char *load_public_key(const char *path)
{
    return load_key(path, "public", crypto_sign_PUBLICKEYBYTES);
}

static unsigned char *load_signing_key(const char *path)
{
    return load_key(path, "private", crypto_sign_SECRETKEYBYTES);
}

static unsigned char *parse_public_key(const char *buffer, size_t buffer_len)
{
    return parse_key(buffer, buffer_len, "public", crypto_sign_PUBLICKEYBYTES);
}

static unsigned char *parse_signing_key(const char *buffer, size_t buffer_len)
{
    return parse_key(buffer, buffer_len, "private", crypto_sign_SECRETKEYBYTES);
}


static void autoselect_mmc_device(struct mmc_device *device)
{
    struct mmc_device devices[16];
    int found_devices = mmc_scan_for_devices(devices, NUM_ELEMENTS(devices));
    if (found_devices == 1) {
        *device = devices[0];
    } else if (found_devices == 0) {
        fwup_errx(EXIT_FAILURE, "No memory cards found. Try reinserting the card.");
    } else {
        fprintf(stderr, "Too many possible memory cards found: \n");
        for (int i = 0; i < found_devices; i++) {
            char sizestr[16];
            format_pretty_auto(devices[i].size, sizestr, sizeof(sizestr));
            fprintf(stderr, "  %s (%s)\n", devices[i].path, sizestr);
        }
        fprintf(stderr, "Automatic selection not possible. Specify one using the -d option.\n");
        exit(EXIT_FAILURE);
    }
}

static char *autoselect_and_confirm_mmc_device(bool accept_found_device, const char *input_firmware)
{
    struct mmc_device device;
    autoselect_mmc_device(&device);
    if (!accept_found_device) {
        if (!input_firmware)
            fwup_errx(EXIT_FAILURE, "Cannot confirm use of %s when using stdin.\nRerun with -y if location is correct.", device.path);

        char sizestr[16];
        format_pretty_auto(device.size, sizestr, sizeof(sizestr));
        fprintf(stderr, "Use %s memory card found at %s? [y/N] ", sizestr, device.path);
        int response = fgetc(stdin);
        if (response != 'y' && response != 'Y')
            fwup_errx(EXIT_FAILURE, "aborted by user");
    }
    return strdup(device.path);
}

static void print_selected_device()
{
    struct mmc_device device;
    autoselect_mmc_device(&device);

    struct simple_string s;
    simple_string_init(&s);
    ssprintf(&s, "%s\n", device.path);
    fwup_output(FRAMING_TYPE_SUCCESS, 0, s.str);
    free(s.str);
}

static void print_detected_devices()
{
    struct mmc_device devices[16];
    int found_devices = mmc_scan_for_devices(devices, NUM_ELEMENTS(devices));

    struct simple_string s;
    simple_string_init(&s);
    for (int i = 0; i < found_devices; i++)
        ssprintf(&s, "%s,%" PRId64"\n", devices[i].path, devices[i].size);
    fwup_output(FRAMING_TYPE_SUCCESS, 0, s.str);
    free(s.str);
}

int main(int argc, char **argv)
{
    const char *configfile = "fwupdate.conf";
    int command = CMD_NONE;

    char *mmc_device_path = NULL;
    const char *input_firmware = NULL;
    const char *output_firmware = NULL;
    const char *task = NULL;
    const char *sparse_check = NULL;
    size_t sparse_check_size = 4096; // Arbitrary default.
    bool accept_found_device = false;
    unsigned char *signing_key = NULL;
    unsigned char *public_keys[FWUP_MAX_PUBLIC_KEYS + 1] = {NULL};
    int num_public_keys = 0;
#if defined(__APPLE__)
    // On hosts, the right behavior for almost all use cases is to eject
    // so that the user can plug the SDCard into their board. Detecting
    // that OSX is a host is easy; Linux, not so much. Luckily, Linux doesn't
    // need an eject.
    bool eject_on_success = true;
#else
    bool eject_on_success = false;
#endif
    bool enable_trim = false;
    bool unmount_first = true;
    bool easy_mode = true;
    bool numeric_progress = false;
    int progress_low = 0;    // 0%
    int progress_high = 100; // to 100%
    int compression_level = 9; // 1 - 9

    if (argc == 1) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    mmc_init();
    atexit(mmc_finalize);

    int opt;
    while ((opt = getopt_long(argc, argv, "acd:DEf:Fghi:lmno:p:qSs:t:VvUuyZz123456789", long_options, NULL)) != -1) {
        switch (opt) {
        case 'a': // --apply
            command = CMD_APPLY;
            easy_mode = false;
            break;
        case 'c': // --create
            command = CMD_CREATE;
            easy_mode = false;
            break;
        case 'd':
            mmc_device_path = optarg;
            break;
        case 'D': // --detect
            print_detected_devices();
            exit(EXIT_SUCCESS);
        case 'E': // --eject
            eject_on_success = true;
            break;
        case 'f':
            configfile = optarg;
            easy_mode = false;
            break;
        case 'F': // --framing
            fwup_framing = true;
            easy_mode = false;
            break;
        case 'g': // --gen-keys
            command = CMD_GENERATE_KEYS;
            easy_mode = false;
            break;
        case 'h':
            print_usage();
            exit(EXIT_SUCCESS);
        case 'i':
            input_firmware = optarg;
            easy_mode = false;
            break;
        case 'l': // --list
            command = CMD_LIST;
            easy_mode = false;
            break;
        case 'm': // --metadata
            command = CMD_METADATA;
            easy_mode = false;
            break;
        case 'o':
            output_firmware = optarg;
            easy_mode = false;
            break;
        case 'p':
            if (num_public_keys < FWUP_MAX_PUBLIC_KEYS) {
                public_keys[num_public_keys] = load_public_key(optarg);
                num_public_keys++;
                easy_mode = false;
            } else
                fwup_warnx("Ignoring public key since only %d supported", FWUP_MAX_PUBLIC_KEYS);

            break;
        case 'n':
            numeric_progress = true;
            break;
        case 'q':
            quiet = true;
            break;
        case 'S': // --sign
            command = CMD_SIGN;
            easy_mode = false;
            break;
        case 's':
            signing_key = load_signing_key(optarg);
            easy_mode = false;
            break;
        case 't': // --task
            task = optarg;
            break;
        case 'v': // --verbose
            fwup_verbose = true;
            break;
        case 'V': // --verify
            command = CMD_VERIFY;
            easy_mode = false;
            break;
        case 'u': // --unmount
            unmount_first = true;
            break;
        case 'U': // --no-unmount
            unmount_first = false;
            break;
        case '+': // --unsafe
            fwup_unsafe = true;
            break;
        case 'y':
            accept_found_device = true;
            break;
        case 'z':
            print_selected_device();
            exit(EXIT_SUCCESS);
        case '!': // --enable-trim
            enable_trim = true;
            break;
        case '@': // --version
            print_version();
            exit(EXIT_SUCCESS);
        case '#': // --no-eject
            eject_on_success = false;
            break;
        case '$': // progress-low
            progress_low = (int) strtol(optarg, 0, 0);
            break;
        case '%': // progress-high
            progress_high = (int) strtol(optarg, 0, 0);
            break;
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            compression_level = opt - '0';
            break;
        case '&': // --sparse-check
            sparse_check = optarg;
            command = CMD_SPARSE_CHECK;
            easy_mode = false;
            break;
        case '*': // --sparse-check-size
            sparse_check_size = strtoul(optarg, 0, 0);
            break;
        case '(': // --private-key
            signing_key = parse_signing_key(optarg, strlen(optarg));
            easy_mode = false;
            break;
        case ')': // --public-key
            if (num_public_keys < FWUP_MAX_PUBLIC_KEYS) {
                public_keys[num_public_keys] = parse_public_key(optarg, strlen(optarg));
                num_public_keys++;
            } else
                fwup_warnx("Ignoring public key since only %d supported", FWUP_MAX_PUBLIC_KEYS);
            break;
        case '~': // --exit-handshake
            atexit(handshake_exit);
            break;
        default: /* '?' */
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

#ifdef _WIN32
    if (fwup_framing) {
        setmode(STDIN_FILENO, O_BINARY);
        setmode(STDOUT_FILENO, O_BINARY);
    }
#endif

    if (quiet && numeric_progress)
        fwup_errx(EXIT_FAILURE, "pick either -n or -q, but not both");

    // Support an easy mode where the user can pass a .fw file
    // and it will program an attached SDCard, etc. with minimal
    // fuss. Some options are supported.
    if (easy_mode && optind == argc - 1) {
        command = CMD_APPLY;
        input_firmware = argv[optind++];
        if (!task)
            task = "complete";
    }

    if (optind < argc)
        fwup_errx(EXIT_FAILURE, "unexpected parameter: %s", argv[optind]);

    // Normalize the firmware filenames in the case that the user wants
    // to use stdin/stdout
    if (input_firmware && strcmp(input_firmware, "-") == 0)
        input_firmware = 0;
    if (output_firmware && strcmp(output_firmware, "-") == 0)
        output_firmware = 0;

    switch (command) {
    case CMD_NONE:
        fwup_errx(EXIT_FAILURE, "specify one of -a, -c, -l, -m, -S, -V, or -z");

    case CMD_APPLY:
    {
        if (!task)
            fwup_errx(EXIT_FAILURE, "specify a task (-t)");

        if (!mmc_device_path)
            mmc_device_path = autoselect_and_confirm_mmc_device(accept_found_device, input_firmware);

        if (quiet)
            fwup_progress_mode = PROGRESS_MODE_OFF;
        else if (fwup_framing)
            fwup_progress_mode = PROGRESS_MODE_FRAMING;
        else if (numeric_progress)
            fwup_progress_mode = PROGRESS_MODE_NUMERIC;
        else
            fwup_progress_mode = PROGRESS_MODE_NORMAL;
        struct fwup_progress progress;
        progress_init(&progress, progress_low, progress_high);

        // Check if the mmc_device_path is really a special device. If
        // we're just creating an image file, then don't try to unmount
        // everything using it.
        bool is_regular_file = will_be_regular_file(mmc_device_path);
        int output_fd;
        if (is_regular_file) {
            // This is a regular file, so open it the regular way.
            output_fd = open(mmc_device_path, O_RDWR | O_CREAT | O_WIN32_BINARY, 0644);

            struct stat st;
            if (output_fd >= 0 && (fstat(output_fd, &st) < 0 || (st.st_mode & 0222) == 0)) {
                // The file permissions are read-only, but the user was able to
                // open it writable. Root can do this. This is almost certainly
                // a mistake so error out. Changing file permissions to make it
                // writable is the way to get around this and the error message
                // below describes it.
                close(output_fd);
                output_fd = -1;
            }
            if (enable_trim) {
                fwup_warnx("ignoring --enable_trim since operating on a regular file");
                enable_trim = false;
            }
        } else {
            // Attempt to unmount everything using the device to avoid corrupting partitions.
            // For partial updates, this just unmounts everything that can be unmounted. Errors
            // are ignored, which is an hacky way of making this do what's necessary automatically.
            // NOTE: It is possible in the future to scan the config file and just unmount partitions
            //       that overlap what will be written.
            if (unmount_first) {
                if (mmc_umount_all(mmc_device_path) < 0)
                    exit(EXIT_FAILURE);
            }

            // Call out to platform-specific code to obtain a filehandle
            output_fd = mmc_open(mmc_device_path);
        }

        // Make sure that the output opened successfully and don't allow the
        // filehandle to be passed to child processes.
        if (output_fd < 0) {
            fprintf(stderr, "\n");
            if (file_exists(mmc_device_path)) {
                fwup_errx(EXIT_FAILURE, "Cannot open '%s' for output.\nCheck file permissions or the read-only tab if this is an SD Card.",
                     mmc_device_path);
            } else {
                fwup_errx(EXIT_FAILURE, "Cannot create '%s'.\nCheck the path and permissions on the containing directory.",
                     mmc_device_path);
            }
        }
#ifdef HAVE_FCNTL
        (void) fcntl(output_fd, F_SETFD, FD_CLOEXEC);
#endif

        if (fwup_apply(input_firmware,
                       task,
                       output_fd,
                       &progress,
                       public_keys,
                       enable_trim) < 0) {
            if (!quiet)
                fprintf(stderr, "\n");
            fwup_errx(EXIT_FAILURE, "%s", last_error());
        }

        if (!is_regular_file && eject_on_success) {
            // On OSX, at least, the system complains bitterly if you don't eject the device when done.
            // This just does whatever is needed so that the device can be removed.
            mmc_eject(mmc_device_path);
        }
        fwup_output(FRAMING_TYPE_SUCCESS, 0, "");
        break;
    }

    case CMD_CREATE:
        if (fwup_create(configfile, output_firmware, signing_key, compression_level) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_LIST:
        if (fwup_list(input_firmware, public_keys) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_METADATA:
        if (fwup_metadata(input_firmware, public_keys) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_GENERATE_KEYS:
        if (fwup_genkeys() < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_SIGN:
        if (fwup_sign(input_firmware, output_firmware, signing_key) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_VERIFY:
        if (fwup_verify(input_firmware, public_keys) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());

        break;

    case CMD_SPARSE_CHECK:
        if (sparse_file_is_supported(sparse_check, sparse_check_size) < 0)
            fwup_errx(EXIT_FAILURE, "%s", last_error());
        else
            fwup_output(FRAMING_TYPE_SUCCESS, 0, "Sparse files supported\n");
    }

    if (signing_key)
        free(signing_key);
    for (int i = 0; i < num_public_keys; i++)
        free(public_keys[i]);

    return 0;
}
