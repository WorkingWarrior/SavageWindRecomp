"""Experimental game identities, separate from game_sites.py's BOTW versions.

TotK support here means recognition for a bounded bring-up, not runtime support.
The build prefix is shared with runtime/game_profile.h; no BOTW sites or patches
are supplied for TotK. Unknown TotK builds must not silently use the 1.0.0 profile.
"""
import os
import re

from game_sites import SiteError


def totk_version(build_id):
    header = os.path.join(os.path.dirname(__file__), '..', 'runtime', 'game_profile.h')
    with open(header) as f:
        match = re.search(r'^#define TOTK_100_BUILD_ID "([0-9a-f]{16})"$', f.read(), re.M)
    if not match:
        raise SiteError('missing TotK experiment build ID in runtime/game_profile.h')
    if build_id[:16].lower() != match.group(1):
        raise SiteError(f'TotK main build {build_id[:16]} is not recognized by this experiment')
    return '1.0.0'
