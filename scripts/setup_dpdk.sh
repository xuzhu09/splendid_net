# 1. 分配大页内存 (1024 * 2MB = 2GB)
echo "[1/5] Allocating hugepages..."
sudo sh -c 'echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages'

# 2. 加载 VFIO 驱动
echo "[2/5] Loading VFIO driver..."
sudo modprobe vfio-pci

# 3. 开启 VFIO 的“不安全模式” (因为虚拟机没有 IOMMU 硬件)
echo "[3/5] Enabling unsafe IOMMU mode..."
sudo sh -c 'echo 1 > /sys/module/vfio/parameters/enable_unsafe_noiommu_mode'

# 4. 关闭网卡 (假设你的 Host-only 网卡是 ens37 / 0000:02:05.0)
echo "[4/5] Downing interface $NIC_INTERFACE..."
sudo ip link set ens37 down

# 5. 绑定网卡给 DPDK
# (请确认你的 dpdk-devbind.py 路径)
echo "[5/5] Binding NIC $NIC_PCI_ID to vfio-pci..."
sudo ~/dpdk-stable-22.11.10/usertools/dpdk-devbind.py -b vfio-pci 0000:02:05.0

# 6. 验证 (可选)
echo "Setup Complete. Current Status:"
sudo ~/dpdk-stable-22.11.10/usertools/dpdk-devbind.py -s
# 看到 0000:02:05.0 对应的 drv=vfio-pci 就成功了