set -o pipefail
export DEBIAN_FRONTEND=noninteractive
sudo -n apt-get install -y libpcl-dev libpcap-dev ros-jazzy-diagnostic-updater ros-jazzy-pcl-conversions ros-jazzy-pluginlib 2>&1 | tail -25
echo "APTEXIT=${PIPESTATUS[0]}"
dpkg -l | grep -c "^ii  libpcl-dev"
