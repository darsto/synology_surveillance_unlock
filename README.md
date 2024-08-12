# Enable 50 cameras in Synology Surveillance Station

Supported version: x86 9.2.0-11289

Modify the default number of allowed cameras from ~2 to 50, and patch out 7 different security
checks fired after certain delays.

Synology Surveillance contains even more security checks, but after this patch they're only fired
after ~27 hours since launch, so a scheduled `synopkg restart SurveillanceStation` once a day
gives us less than 30 seconds of camera downtime per day, which (to me) is acceptable.

## Installation

Build with gcc & patchelf either on a recent x86 linux, or directly on the NAS:
```
$ cd synology_surveillance_unlock
$ make
$ ls -lh libssutils.mitm.so
[...] 18K libssutils.mitm.so
```

Then on the NAS:
```
$ cd /var/packages/SurveillanceStation/target/lib
$ mv libssutils.so libssutils.org.so
$ cp /path/to/libssutils.mitm.so ./
```

In the web panel add a daily task at any desired hour: (to be run as root)
```
/usr/syno/bin/synopkg restart SurveillanceStation
```

The above restart will show an annoying alert() in any active browser session,
so we can patch that out too by appending one line to sds.js:
```
# tail -n 1 /var/packages/SurveillanceStation/target/ui/sds.js
SYNO.API.RedirectToDSMByErrorCode = () => { };
```

## How it works

The number of cameras in the default license is hardcoded in multiple libraries and executables.
The "effective" number is hardcoded in libssutils.so which is loaded by most Synology Surveillance
executables. If only that one number is patched, Synology Surveillance will appear to work, but
will break after a few hours. Then it would require a manual service restart - only to break
again after those few hours.

We provide a man-in-the-middle libssutils.so which:
 - loads the original libssutils.so (that was renamed to libsssutils.org.so)
 - patches the default number of licenses to 50
 - patches out security checks in the current executable, depending on the current executable name

The security checks are in 5 different executables. Conveniently all of them load libssutils.so.
