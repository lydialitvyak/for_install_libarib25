b25.exe ver.0.2.1 ��Linux�Ή��łł��B
arib25�f�B���N�g����make�ƑłĂ�arib25/src/b25�Ƀr���h����܂��B
�g�p���C�u������PCSC-Lite(http://pcsclite.alioth.debian.org/)�ŁA�J�[�h���[�_���g�p�\�ȏ�ԂɂȂ��Ă��邱�Ƃ��K�v�ł��B
Windows�ł͓S��HX-520UJJ��Linux�ł͎g�p�ł��Ȃ��̂Œ��ӂ��K�v�ł��B
NTTCom��SCR3310-NTTCom�y��athena-scs.hs.shopserve.jp/SHOP/RW001.html�œ���񍐂�����܂��B
B-CAS�J�[�h�̕\���ɒ��ӂ��ĉ������B�`�b�v������ʂ���ł��B
�����ł̔��}�����̃J�[�h���[�_���g�p����ꍇ�́APCSC-Lite�̃y�[�W�ɂ���CCID driver���g�p���A
/etc/libccid_Info.plist����
====
	<key>ifdVendorID</key>
	<array>
		<string>0xXXXX</string>
		         ��
	</array>

	<key>ifdProductID</key>
	<array>
		<string>0xXXXX</string>
		         ��
	</array>

	<key>ifdFriendlyName</key>
	<array>
		<string>XXXXXXXXXXXXXXXXXX</string>
		               ��
	</array>
====
������zip���ɂ��铯���̃t�@�C���̓��������Ɠ���ւ��ĉ������B
�����̃t�@�C���ʒu��Debian�ˑ���������܂���B
  Gentoo�ł�/usr/lib/readers/usb/ifd-ccid.bundle/Contents/Info.plist�ɂ������Ƃ����񍐂�����܂����B
���}�y�эŋ߂̔��}�ł̓J�[�h���[�_���ς���Ă���ׁA/etc/libccid_Info.plist�̕ύX�͕s�v�ł��B

smartcard_list.txt��PCSC-Lite�Ɋ܂܂��pcsc_scan�p�ł��B
PCSC-Lite�̃t�@�C���ƒu���������B-CAS��B-CAS�Ƃ��ĔF������܂��B

PCSC-Lite��API��Windows�X�}�[�g�J�[�h�A�N�Z�X�pAPI�ƌ݊��ł������ׁA
�ق�Linux�ŃR���p�C���G���[�ɂȂ镔���̑Ώ��݂̂ł��B
ver.0.2.0���32bit����2Gbyte�t�@�C���̖�肪�Ȃ��Ȃ����͂��ł��B64bit���ł�2Gbyte�ȏ�̃t�@�C���������o���邱�Ƃ��m�F���Ă��܂��B

PCSC-Lite�Ɋ܂܂��DWORD���̒�`�͔�32bit���̏ꍇ�ɖ�肪����܂����A�֌W�Ȃ������܂����B

�ύX�_:
ver.0.1.2�ɑ΂���p�b�`��open�̈���������Ă��܂��Ă����ӏ������ɖ߂��܂����B
ver.0.1.5�ŏo�̓t�@�C���̃p�[�~�b�V�������K���������̂𒼂��܂����Bumask�ɏ]���悤�ɂȂ��Ă���͂��ł��B
ver.0.2.0�ŁA32bitLinux���2GByte�ȏ�̃t�@�C���������ł��Ȃ��o�O���C�������͂��ł��B

extrecd���g�p���Ă�����ւ̒��ӓ_:
b25�̌Ăяo������-p 0 -v 0�I�v�V������t����K�v������܂�(b25���牽���o�͂�����΃G���[�Ƃ݂Ȃ��Ă����)�B�ȉ��̏C�����s�Ȃ��ĉ������B
430�s��
my @b25_cmd    = (@b25_prefix, $path_b25, $target_encts, $target_ts);
��
my @b25_cmd    = (@b25_prefix, $path_b25, "-p", "0", "-v", "0", $target_encts, $target_ts);

���C�Z���X:
���̃\�[�X�͂܂�����쐬��b25�قڂ��̂܂܂Ȃ̂ŁA�܂�����̔��f�ɏ]���܂��B
�����arib25/readme.txt�ɂ���I���W�i��b25�ɓY�t����Ă���readme.txt�ɏ����Ă���A
>�@�E�\�[�X�R�[�h�𗘗p�������Ƃɂ���āA������̃g���u�����������Ă�
>�@�@�Ζ� �a�m�͐ӔC�𕉂�Ȃ�
>�@�E�\�[�X�R�[�h�𗘗p�������Ƃɂ���āA�v���O�����ɖ�肪�������Ă�
>�@�@�Ζ� �a�m�͐ӔC�𕉂�Ȃ�
>
>�@��L 2 �����ɓ��ӂ��č쐬���ꂽ�񎟓I���앨�ɑ΂��āA�Ζ� �a�m��
>�@������҂ɗ^�����鏔�������s�g���Ȃ�
���K�p����܂��B

���̑�:
���̃v���O������AS-IS�Œ񋟂���܂��B�Ȃɂ���肪�N�����Ă��ӔC�͎��Ă܂���B

����m�F��:
  Debian GNU/Linux lenny(testing)
  Linux 2.6.22.6 SMP PREEMPT x86_64

��N/E9PqspSk
