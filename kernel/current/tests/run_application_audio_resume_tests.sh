#!/bin/sh
set -eu
project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
image=local/c-builder:2026.08.02-kernel
kernel_source=${GKD_AUDIO_REGCACHE_SOURCE:-/opt/gkd-build/private-state/gkd-mini-system-rebuild/kernel-sources/ingenic-community-linux-6.1}
[ "$(docker image inspect "$image" --format '{{.Id}}')" = sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1 ]
[ -d "$kernel_source/.git" ]
[ "$(git -C "$kernel_source" rev-parse HEAD)" = 91fe78280ac7dd0dae0f58cb271e821bd39ba97e ]
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$project:/project:ro" -v "$kernel_source:/kernel-source:ro" "$image" sh -eu -c '
 mkdir -p /tmp/audio-old /tmp/audio-new /tmp/audio-r12
 python3 /project/kernel/current/tests/extract_application_audio_resume_test.py --baseline --output /tmp/audio-old/codec-under-test.h --config-output /tmp/audio-old/es8323-config-under-test.h
 python3 /project/kernel/current/tests/extract_application_audio_resume_test.py --output /tmp/audio-new/codec-under-test.h --config-output /tmp/audio-new/es8323-config-under-test.h --regcache-source /kernel-source --regcache-output /tmp/audio-new/regcache-under-test.h
 cp /tmp/audio-new/codec-under-test.h /tmp/audio-new/regcache-under-test.h /tmp/audio-r12/
 cp /tmp/audio-old/es8323-config-under-test.h /tmp/audio-r12/
 cc -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -DPATCHED=0 -DEXPECT_SINGLE_WRITE=0 -I/tmp/audio-old -I/tmp/audio-new /project/kernel/current/tests/audio_resume_fixture.c -o /tmp/audio-old/test
 /tmp/audio-old/test
 cc -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -DPATCHED=1 -DEXPECT_SINGLE_WRITE=0 -I/tmp/audio-r12 /project/kernel/current/tests/audio_resume_fixture.c -o /tmp/audio-r12/test
 /tmp/audio-r12/test
 cc -std=gnu99 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -DPATCHED=1 -DEXPECT_SINGLE_WRITE=1 -I/tmp/audio-new /project/kernel/current/tests/audio_resume_fixture.c -o /tmp/audio-new/test
 /tmp/audio-new/test
 cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPATCHED=1 -DEXPECT_SINGLE_WRITE=0 -I/tmp/audio-r12 /project/kernel/current/tests/audio_resume_fixture.c -o /tmp/audio-r12/test-sanitized
 /tmp/audio-r12/test-sanitized
 cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -fsanitize=address,undefined -DPATCHED=1 -DEXPECT_SINGLE_WRITE=1 -I/tmp/audio-new /project/kernel/current/tests/audio_resume_fixture.c -o /tmp/audio-new/test-sanitized
 /tmp/audio-new/test-sanitized
 python3 /project/kernel/current/tests/test_profile_config.py
'
