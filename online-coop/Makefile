B ?= ../build/online-coop
CXX ?= c++
CPPFLAGS += -Iinclude -Isrc
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -pthread
LDLIBS += -pthread
ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
endif
.PHONY: all test clean
all: $(B)/coop-peer $(B)/libxcx-coop.a
$(B):
	mkdir -p $(B)
$(B)/coop.o: src/coop.cpp src/protocol.h include/xcx_coop/coop.h | $(B)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(B)/libxcx-coop.a: $(B)/coop.o
	$(AR) rcs $@ $^
$(B)/coop-peer: tools/peer.cpp $(B)/libxcx-coop.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(B)/libxcx-coop.a $(LDLIBS) -o $@
$(B)/protocol-test: tests/protocol.cpp $(B)/libxcx-coop.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(B)/libxcx-coop.a $(LDLIBS) -o $@
$(B)/party-test: tests/party.cpp $(B)/libxcx-coop.a
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(B)/libxcx-coop.a $(LDLIBS) -o $@
$(B)/pose-link-test: tests/pose_link.c ../runtime/online_coop_pose.c ../runtime/online_coop_link.c
	$(CC) -std=c11 -O2 -g -Wall -Wextra -D_POSIX_C_SOURCE=200809L -I../runtime $^ -lm -o $@
$(B)/pose-test: tests/pose.cpp provider/game_pose.h
	$(CXX) $(CPPFLAGS) -Iprovider $(CXXFLAGS) $< $(LDLIBS) -o $@
test: all $(B)/protocol-test $(B)/party-test $(B)/pose-test $(B)/pose-link-test
	./$(B)/protocol-test
	./$(B)/party-test
	./$(B)/pose-test
	./$(B)/pose-link-test
	python3 -m unittest discover -s tests -v
clean:
	rm -rf $(B)
