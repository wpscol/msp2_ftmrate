-include .env.example
-include .env

default: init

init: cpenv download link build
	mkdir -p output

cpenv:
	cp -nf .env.example .env

download:
	wget -O ns-allinone-3.36.1.tar.bz2 "https://www.nsnam.org/releases/ns-allinone-3.36.1.tar.bz2"
	tar -xvf ns-allinone-3.36.1.tar.bz2
	rm -rf ns-allinone-3.36.1.tar.bz2

clean:
	rm -rf $(NS3_DIR)/build
	rm -rf $(NS3_DIR)/cmake-cache
	rm -rf .venv
	rm -rf ml4wifi.egg-info
	rm -rf $(NS3_DIR)/contrib
	rm -rf $(NS3_DIR)/scratch
	rm -rf .vscode
	rm -rf ns-allinone-3.36.1.tar.bz2
	rm -rf output

build:
	$(NS3_BIN) configure --build-profile=optimized --enable-examples --enable-tests
	$(NS3_BIN) build "scratch/moving.cc"
	$(NS3_BIN) build "scratch/stations.cc"

link:
	rm -rf $(NS3_DIR)/scratch
	rm -rf $(NS3_DIR)/contrib
	ln -s $(shell pwd)/ns3_files/scratch $(NS3_DIR)/scratch
	ln -s $(shell pwd)/ns3_files/contrib $(NS3_DIR)/contrib

run-moving:
	$(NS3_MOVING_BIN) \
		--manager="ns3::MinstrelHtWifiManager" \
		--managerName="mistrel-ht" \
		--velocity=0 \
		--simulationTime=60 \
		--warmupTime=5 \
		--fuzzTime=1 \
		--measurementsInterval=1 \
		--lossModel=Nakagami \
		--RngRun=100 \
		--csvPath="./results_moving.csv" \
		--tcpCongestionAlg="ns3::TcpWestwood" \
		--wallInterval=0 \
		--wallLoss=0

run-stations:
	$(NS3_STATIONS_BIN) \
		--manager="ns3::MinstrelHtWifiManager" \
		--managerName="mistrel-ht" \
		--simulationTime=60 \
		--warmupTime=5 \
		--lossModel=Nakagami \
		--RngRun=100 \
		--csvPath="./results_stations.csv" \
		--mobilityModel=RWPM \
		--nodeSpeed=10.0 \
		--nodePause=5.0