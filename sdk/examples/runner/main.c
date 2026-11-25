/*
 * The runner ROM. It reads the tape's name from /PIPBOY/RUNNER.CFG,
 * launches that tape once and prints how it ended. It draws nothing and
 * reads no key. Its lines and the key that signs them are defined in
 * RobCo-Core's docs/spec/SDK_PACKAGE.md, "The runner ROM".
 *
 * The key lives only in automatic variables of pip_main(), so a tape,
 * which cannot read the ROM's stack, cannot learn it. This file must
 * keep no static object: the build checks that .data and .bss are empty.
 */
#include <pipos/pip.h>

#define CFG_PATH  "/PIPBOY/RUNNER.CFG"
#define CFG_MAX   255
#define NAME_MAX  12

static int config_fail(const char *why, int32_t rc)
{
    pip_printf("@RC stage=runner ev=done result=config rc=%d why=%s\n", (int)rc, why);
    return 1;
}

static const char *result_of(int32_t rc)
{
    if (rc >= 0 && rc <= 255) {
        return "exit";
    }
    switch (-rc) {
    case PIP_EFAULT:  return "fault";
    case PIP_ENOENT:  return "missing";
    case PIP_ENOEXEC: return "bad";
    case PIP_EIO:     return "io";
    case PIP_ENODEV:  return "eject";
    default:          return "error";
    }
}

int pip_main(const pipos_startup_t *startup)
{
    char buf[CFG_MAX + 1];
    char name[NAME_MAX + 1];
    uint32_t n = 0;
    int32_t fd, rc, k1, k2;
    size_t i, len;

    (void)startup;

    fd = pip_file_open(CFG_PATH);
    if (fd < 0) {
        return config_fail("open", fd);
    }
    for (;;) {
        rc = pip_file_read(fd, buf + n, (uint32_t)sizeof(buf) - n);
        if (rc <= 0) {
            break;
        }
        n += (uint32_t)rc;
        if (n == sizeof(buf)) {
            break;
        }
    }
    (void)pip_file_close(fd);
    if (rc < 0) {
        return config_fail("read", rc);
    }
    if (n < 1 || n > CFG_MAX) {
        return config_fail("size", 0);
    }

    rc = pip_ini_get(buf, n, "run", "tape", name, sizeof(name));
    if (rc != 0) {
        return config_fail("parse", rc);
    }
    len = strlen(name);
    if (len < 1 || len > NAME_MAX) {
        return config_fail("name", 0);
    }
    for (i = 0; i < len; i++) {
        if (name[i] < 0x21 || name[i] > 0x7E) {
            return config_fail("name", 0);
        }
    }

    k1 = pip_rand();
    if (k1 < 0) {
        return config_fail("rand", k1);
    }
    k2 = pip_rand();
    if (k2 < 0) {
        return config_fail("rand", k2);
    }

    pip_printf("@RC stage=runner ev=run tape=%s key=%08x%08x\n", name,
               (unsigned)k1, (unsigned)k2);
    rc = pip_holo_launch(name, "");
    pip_printf("@RC stage=runner ev=done result=%s rc=%d key=%08x%08x\n", result_of(rc),
               (int)rc, (unsigned)k1, (unsigned)k2);
    return rc >= 0 && rc <= 255 ? 0 : 1;
}
