
 VNCDOS
--------------
 Purposes. A VNC client for DOS
 Release. Current release is 0.4.
 Features. You can connect or disconnect to VNC servers over any supported
 	packet driver. When connected you can type and see the results
 	on monochrome graphics screen. The program can emulate mouse.
 Instrumental. The program enterly written in C (Turbo C++ v.1.0)
 	and utilize WATTCP stack library.
 Inspirations. This program was inspired by VT52DOS project.
 	Most of the code was rewriten.
 Licence. The program is public-domain.
 
 INSTALLATION
--------------
 Installaion basics. Simply unpack vncdos.exe. You can use it as usual WATTCP
 	program. Now, it needs the WATTCP.CFG file or correctly set of
 	environment variables to work with packet driver.
 	A WATTCP.CFG usually contains some data, check the file and
 	place correct values for you. Example:
my_ip=192.168.76.78 
netmask=255.255.255.0 
gateway=192.168.76.77 
nameserver=192.168.76.77
 
 Example of pppd for DOS. You can use automatic generated WATTCP.CFG with PPPD.
I used DOSPPP06 and so it works. Now sample scriptings, start with GO.BAT:

GO.BAT:
  pppd.exe file connect.opt
  call ip-up.bat
  echo # make auto >wattcp.cfg
  echo my_ip=%myip% >>wattcp.cfg
  echo netmask=%netmask% >>wattcp.cfg
  echo gateway=%remip% >>wattcp.cfg
  echo nameserver=YOUR_NAMESERVER_UP >>wattcp.cfg

CONNECT.OPT:
  COM1
  57600
  modem
  crtscts
  asyncmap 0
  connect "chat -f ppp.scr"
  user "USERNAME"
  passwd "PASSWORD"

PPP.SCR:
  ECHO ON
  ABORT BUSY
  ABORT 'NO CARRIER'
  '' ATZ
  OK ATD######PHONE NUMBER######
  TIMEOUT 120
  CONNECT

 Well, you can connect direct by serial cable with apropriate installed PPPD
	on another host. This way you can provide another CONNECT.OPT file:
CONNECT.OPT:
  COM1
  57600
  local
  crtscts

 That's all.

 USAGE
--------------
 Program start. First you need to start packet driver as described above. Check
 	it in the memory. Then start program with parameter of another
 	host name with the VNC server started:

 	C:\> VNCDOS.EXE SERVER.AT.REAL.COM

 By default program connect to VNC-display 1. If you need connect with another
	display set the port number (the port number for
	display :0 is 5900, for display :1 is 5901, etc..). This command
	will connect to display number 9:

 	C:\> VNCDOS.EXE SERVER.AT.REAL.COM 5909
	 
 Start troubles. If you not able to connect check packet driver existense, host
 	names, utilized ports. Also you should set 8 bits per pixel color
 	depth in your VNC server. This program refreshes only 640x480 screen
 	area and if the server provide most resolution you will see only
 	left top 640x480 area of your desktop.

 Exit key. The main exit key is ALT-ESC. Now you can not use this key
 	combination in remotely started program :(

 Mouse Emulation key. To activate mouse emulation press ALT-~. now you can
 	move the pointer with keyboard arrows. when press 1 or 2 or 3 will
 	click virtual mouse buttons 1,2 or 3. for button hold you should
 	press SHIFT-1, SHIFT-2 or SHIFT-3. after all pointer movements
 	can press any non-mouse-emulated key to leave emulation mode, for
 	example: ENTER, ALT-~ or ESC.

 COMPILING
--------------
 Instrumental. To compile the souces you need 'make' tool of Borland
 	toolset. Another meaning part is C-compiler and linker.
 	This process keep in mind you familar with compilers. Before start
 	compiling please check 'makefile' and set correct paths to
 	the programs.
 Optimizations. If you provide optimisation options like '-a', you can see
 	that program is not work at all. hmm.. so strange. you may play
 	with #pragma's and othe magic. Also you can compile with another
 	memory model than SMALL. try it.
 Troubleshooting. Hmm...

 CONTACTS
--------------
 With any suggestions you can contact with me, Arcady Antipin.
 E-mail: arc@ufachem.eu.org

