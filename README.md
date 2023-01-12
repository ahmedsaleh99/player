## Build Player Lib from source for ubuntu
befor we start we need to install some dependencies we need to biuld our Player Lib as follows
update and upgrade your system using the follwoing two commands and the reboot your system
sudo apt update
sudo apt upgrade
after that install the following dependencies using the command 
sudo apt-get install python3.6
sudo apt-get install python3.6-dev
now we want python3 to be our default python in linux use the following command to do this

sudo  update-alternatives --install /usr/bin/python python /usr/bin/python3 1
python --version

sudo apt install cmake  libboost-all-dev libgeos-dev libgnomecanvas2*   libstatgrab-dev libgsl-dev libv4l-dev  libc6-dev libc6-dev-* libopenexr-dev  libavahi-compat-libdnssd-dev swig  libraw1394-dev libv4l-dev libopencv-dev python3-opencv freeglut3-dev  libserial-dev libusb-1.0-0-dev libgeos* -y 

sudo apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav gstreamer1.0-doc gstreamer1.0-tools gstreamer1.0-x gstreamer1.0-alsa gstreamer1.0-gl gstreamer1.0-gtk3 gstreamer1.0-qt5 gstreamer1.0-pulseaudio -y

now we can move to the biulding procsess
first we need to clone the player lib source code from repo at https://github.com/playerproject/player.git
using the dollowing command (you should have git installed) in your terminal
git clone https://github.com/playerproject/player.git  
we can biuld from the master bransh ( has the latest updates and bug fixes but mybe unstable)
this will dwonload the source code in a folder called player 
move to that folder and  create a biuld folder inside  and move to it using the following commands
cd player
#here we will biuld from the latest stable branch which is release-3-1-patches so we need to checkout this bransh first. type the following command in your terminal
#git checkout release-3-1-patches
mkdir build
cd build
now we need to export the following variable if we want to biuld the python bindings for the player lib by typing the following command
export PYTHON_LIBRARIES=/usr/lib/python3.6/config-3.6m-aarch64-linux-gnu/libpython3.6m.so

if you want to biuld python bindings for client library (which we want) open the following two cmake files  
<player lib source code directory>client_libs/libplayerc/bindings/python/CMakeLists.txt
<player lib source code directory>client_libs/libplayerc++/bindings/python/CMakeLists.txt
and makesure that we anabled biuld python binding options by setting it to ON as follow

OPTION (BUILD_PYTHONC_BINDINGS "Build the Python bindings for the C client library" ON)
OPTION (BUILD_PYTHONCPP_BINDINGS "Build the Python bindings for the C++ client library" ON)

now type  cmake ../ to start the building process. 
after few minitues it should tell you that configuration finished without errors
now type 
make
sudo make install
and we now have compiled and installed the palyer successully fro ubuntu LTS 20.20
last thing is to add the following lines to your .bashrc file 
export PATH=$PATH:"/usr/local/lib64"
export PYTHONPATH=$PYTHONPATH:"/usr/local/lib/python3.6/site-packages"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:"/usr/local/lib":"/usr/local/lib64"
export PLAYERPATH="/usr/local/lib":"/usr/local/lib64"
export STAGEPATH="/usr/local/lib":"/usr/local/lib64"
export OPENBLAS_CORETYPE=ARMV8
# to give the sensors (lidar, sonar, motion sensors, etc, ) connected via serial port static names we have to add some configuration files in the linux
we need to add the following lines to the following file /etc/udev/rules.d/49-custom.rules
open the file as a sudo using any text editor ie  gedit:
sudo gedit  /etc/udev/rules.d/49-custom.rules

add the following lines to the file and save it 

KERNEL=="ttyUSB*", KERNELS=="1-2.1.1:1.0", SYMLINK="right_robot_board_serial_socket"
KERNEL=="ttyUSB*", KERNELS=="1-2.1.2:1.0", SYMLINK="electronic_compass_0"
KERNEL=="ttyUSB*", KERNELS=="1-2.1.3:1.0", SYMLINK="sensor_2"
KERNEL=="ttyUSB*", KERNELS=="1-2.1.4:1.0", SYMLINK="sensor_3"
KERNEL=="ttyUSB*", KERNELS=="1-2.2.1:1.0", SYMLINK="rflex_0"
KERNEL=="ttyUSB*", KERNELS=="1-2.2.2:1.0", SYMLINK="laser_0"
KERNEL=="ttyUSB*", KERNELS=="1-2.2.3:1.0", SYMLINK="camera_0"
KERNEL=="ttyUSB*", KERNELS=="1-2.2.4:1.0", SYMLINK="external_sp_0"

each line of the above gives a fixed name defined by the SYMLINK parameter to a certain serial port defined by the KERNELS parameter 
for example the line 
KERNEL=="ttyUSB*", KERNELS=="1-2.2.2:1.0", SYMLINK="laser_0"
gives the name laser_0 to the device connected to 1-2.2.4:1.0. 
these numbers 1-2.2.2:1.0 is a unique code that maps hardware usb ports connected to our board. 
in this example the code can be interpreted as follows:  
1-2: the second usb main port of my baord. 
2: the second usb connector (my board have 4).
2: in my case i connect to this usb port an extranal usb hub wwith 4 usb connection this is the second usb of thw external usb hub.
use the folowing command to list the connected usd to your system
lsusb -t
you should get something like this:
/:  Bus 02.Port 1: Dev 1, Class=root_hub, Driver=tegra-xusb/4p, 5000M
    |__ Port 1: Dev 2, If 0, Class=Hub, Driver=hub/4p, 5000M
/:  Bus 01.Port 1: Dev 1, Class=root_hub, Driver=tegra-xusb/5p, 480M
    |__ Port 2: Dev 2, If 0, Class=Hub, Driver=hub/4p, 480M
        |__ Port 1: Dev 3, If 0, Class=Hub, Driver=hub/4p, 480M
            |__ Port 1: Dev 6, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 2: Dev 7, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 3: Dev 9, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 4: Dev 11, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
        |__ Port 2: Dev 4, If 0, Class=Hub, Driver=hub/4p, 12M
            |__ Port 2: Dev 10, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 3: Dev 12, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 1: Dev 8, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
            |__ Port 4: Dev 13, If 0, Class=Vendor Specific Class, Driver=pl2303, 12M
        |__ Port 3: Dev 5, If 0, Class=Vendor Specific Class, Driver=rt2800usb, 480M

now we want to reload these updates to be reflected in the system by using the following command.

sudo udevadm control --reload-rules && sudo udevadm trigger

to make sure every thing is ok type the following command in the terminla you should see the new device names  there 
ls /dev/
for more details you can check the following tutorial:
https://msadowski.github.io/linux-static-port/