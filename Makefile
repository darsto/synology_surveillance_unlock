all:
	gcc -o libssutils.mitm.so -O2 -shared -fPIC -masm=intel lib.c
	patchelf --add-needed /var/packages/SurveillanceStation/target/lib/libssutils.org.so libssutils.mitm.so
