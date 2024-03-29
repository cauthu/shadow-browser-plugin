#!/usr/bin/env python2.7

#
# script to generate shadow.config.xml for tor experiments, originally
# copied from shadow-plugin-tor repository at commit:
#
# commit b7e5a9c3ad63decdd1b90b17cb09dd831459777f
# Author: Rob Jansen <jansen@cs.umn.edu>
# Date:   Wed Jun 1 15:54:00 2016 -0400
#
#     Correct consensus bandwidth unit to KiB/s
#
#     thanks @swojo!
#
#
# original instructions to use this are at
# https://github.com/shadow/shadow-plugin-tor/wiki#generating-a-new-tor-network
#

import os, sys, subprocess, argparse, time, shlex, shutil
from random import choice
import random
from datetime import datetime
from numpy import mean
from lxml import etree
from networkx import DiGraph, write_graphml
from collections import defaultdict

# This should NOT be expanded, we'll use this directly in the XML file
INSTALLPREFIX="~/.shadow/"

# distribution of CPU frequencies, in KHz
CPUFREQS=["2200000", "2400000", "2600000", "2800000", "3000000", "3200000", "3400000"]

NRELAYS = 10
NBRIDGES = 0
NAUTHS = 1
NBRIDGEAUTHS = 0
NCLIENTS = 100
NBRIDGECLIENTS = 0

NSERVERS = 10
FWEB = 0.97
FBULK = 0.03
NPERF50K = 0.0
NPERF1M = 0.0
NPERF5M = 0.0

SEED = 7281353

# have tgen listen on this to leave 80 for my webserver
TGEN_SERVERPORT = 90

newweb_conf_dir = 'newweb_conf'

ioservice_conf_fpath = '{}/ioservice.conf'.format(newweb_conf_dir)
renderer_conf_fpath = '{}/renderer.conf'.format(newweb_conf_dir)
driver_conf_fpath = '{}/driver.conf'.format(newweb_conf_dir)
tproxy_csp_conf_fpath = '{}/tproxy_csp.conf'.format(newweb_conf_dir)
tproxy_ssp_conf_fpath = '{}/tproxy_ssp.conf'.format(newweb_conf_dir)

ioservice_args = '--conf={}'.format(ioservice_conf_fpath)
renderer_args = '--conf={}'.format(renderer_conf_fpath)
driver_args = '--conf={}'.format(driver_conf_fpath)
tproxy_csp_args = '--conf={}'.format(tproxy_csp_conf_fpath)
tproxy_ssp_args = '--conf={}'.format(tproxy_ssp_conf_fpath)
webserver_args = '--port=80 --port=443'

ioservice_pluginid = 'ioservice'
renderer_pluginid = 'renderer'
driver_pluginid = 'driver'
tproxy_pluginid = 'tproxy'
webserver_pluginid = 'webserver'

class Relay():
    def __init__(self, ip, bw, isExit=False, isGuard=False):
        self.ip = ip
        self.bwconsensus = int(bw) # in KiB, from consensus
        self.isExit = isExit
        self.isGuard = isGuard
        self.code = None

        self.bwrate = 0 # in bytes
        self.bwburst = 0 # in bytes
        self.bwtstamp = 0

        self.maxobserved = 0 # in bytes
        self.maxread = 0 # in bytes
        self.maxwrite = 0 # in bytes

        self.upload = 0 # in KiB
        self.download = 0 # in KiB

        self.rates = [] # list of bytes/s histories

    def getBWRateArg(self): # the min required Tor config
        return 76800 if self.bwrate < 76800 else self.bwrate

    def getBWBurstArg(self):
        return 76800 if self.bwburst < 76800 else self.bwburst

    def getBWConsensusArg(self):
        return 1000 if self.bwconsensus <= 0 else self.bwconsensus

    def setRegionCode(self, code):
        self.code = code

    def setLimits(self, bwrate, bwburst, bwtstamp):
        # defaults are 5MiB rate (5120000), 10MiB Burst (10240000)
        # minimums are 30KiB rate (30720)
        if bwtstamp > self.bwtstamp:
            self.bwtstamp = bwtstamp
            self.bwrate = int(bwrate)
            self.bwburst = int(bwburst)

    # max observed from server descriptor (min of max-read and max-write over 10 second intervals)
    def setMaxObserved(self, observed):
        self.maxobserved = max(self.maxobserved, observed)

    # max read and write over 15 minute intervals from extra-infos
    def setMaxSpeeds(self, read, write):
        self.maxread = max(self.maxread, read)
        self.maxwrite = max(self.maxwrite, write)

    def computeSpeeds(self):
        '''
        compute the link speeds to the ISP

        we prefer relay measurements over the consensus, because the measurement
        is generally more accurate and unlikely malicious

        we can estimate the link speed as the maximum bandwidth we've ever seen.
        this is usually the observed bandwidth from the server descriptor since
        its computed over 10 second intervals rather than the read/write histories
        from the extra-info which are averaged over 15 minutes.

        since the observed bandwidth reported in the server descriptor is the minimum
        of the 10 second interval reads and writes, we use the extra-infos
        to determine which of read or write this observed bandwidth likely
        corresponds to. then we compute the missing observed value (the max of the
        10 second interval reads and writes) using the ratio of read/write from the
        extra-info.

        in the absence of historical data we fall back to the consensus bandwidth
        and hope that TorFlow accurately measured in this case
        '''
        if self.maxobserved > 0:
            if self.maxread > 0 and self.maxwrite > 0:
                # yay, best case as we have all the data
                readToWriteRatio = float(self.maxread)/float(self.maxwrite)
                bw = int(self.maxobserved / 1024.0)
                if readToWriteRatio > 1.0:
                    # write is the min and therefore the 'observed' value
                    self.upload = bw # the observed min
                    self.download = int(bw*readToWriteRatio) # the scaled up max
                else:
                    # read is the min and therefore the 'observed' value
                    self.download = bw # the observed min
                    self.upload = int(bw*(1.0/readToWriteRatio)) # the scaled up max
            else:
                # ok, at least use our observed
                bw = int(self.maxobserved / 1024.0)
                self.download = bw
                self.upload = bw
        elif self.maxread > 0 and self.maxwrite > 0:
            # use read/write directly
            self.download = int(self.maxread / 1024.0)
            self.upload = int(self.maxwrite / 1024.0)
        else:
            # pity...
            bw = int(self.bwconsensus) # in KiB
            self.download = bw
            self.upload = bw

        # the 'tiered' approach, not currently used
        '''
        if self.ispbandwidth <= 512: self.ispbandwidth = 512
        elif self.ispbandwidth <= 1024: self.ispbandwidth = 1024 # 1 MiB
        elif self.ispbandwidth <= 10240: self.ispbandwidth = 10240 # 10 MiB
        elif self.ispbandwidth <= 25600: self.ispbandwidth = 25600 # 25 MiB
        elif self.ispbandwidth <= 51200: self.ispbandwidth = 51200 # 50 MiB
        elif self.ispbandwidth <= 76800: self.ispbandwidth = 76800 # 75 MiB
        elif self.ispbandwidth <= 102400: self.ispbandwidth = 102400 # 100 MiB
        elif self.ispbandwidth <= 153600: self.ispbandwidth = 153600 # 150 MiB
        else: self.ispbandwidth = 204800
        '''

    CSVHEADER = "IP,CCode,IsExit,IsGuard,Consensus(KB/s),Rate(KiB/s),Burst(KiB/s),MaxObserved(KiB/s),MaxRead(KiB/s),MaxWrite(KiB/s),LinkDown(KiB/s),LinkUp(KiB/s),Load(KiB/s)"

    def toCSV(self):
        c = str(int(self.bwconsensus/1000.0)) # should be KB, just like in consensus
        r = str(int(self.bwrate/1024.0))
        b = str(int(self.bwburst/1024.0))
        mo = str(int(self.maxobserved/1024.0))
        mr = str(int(self.maxread/1024.0))
        mw = str(int(self.maxwrite/1024.0))
        ldown = str(int(self.download))
        lup = str(int(self.upload))
        load = str(0)
        if len(self.rates) > 0: load = str(int(mean(self.rates)/1024.0))
        return ",".join([self.ip, self.code, str(self.isExit), str(self.isGuard), c, r, b, mo, mr, mw, ldown, lup, load])

class GeoIPEntry():
    def __init__(self, lownum, highnum, countrycode):
        self.lownum = int(lownum)
        self.highnum = int(highnum)
        self.countrycode = countrycode

def main():
    ap = argparse.ArgumentParser(description='Generate shadow.config.xml file for Tor experiments in Shadow', formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    # configuration options
    ap.add_argument('-p', '--prefix', action="store", dest="prefix", help="PATH to base Shadow installation", metavar="PATH", default=INSTALLPREFIX)
    ap.add_argument('--nauths', action="store", type=int, dest="nauths", help="number N of total directory authorities for the generated topology", metavar='N', default=NAUTHS)
    ap.add_argument('--nbridgeauths', action="store", type=int, dest="nbridgeauths", help="number N of bridge authorities for the generated topology (0 or 1)", metavar='N', default=NBRIDGEAUTHS)
    ap.add_argument('--nrelays', action="store", type=int, dest="nrelays", help="number N of total relays for the generated topology", metavar='N', default=NRELAYS)
    ap.add_argument('--nbridges', action="store", type=int, dest="nbridges", help="number N of total bridges for the generated topology", metavar='N', default=NBRIDGES)
    ap.add_argument('--nclients', action="store", type=int, dest="nclients", help="number N of total clients for the generated topology", metavar='N', default=NCLIENTS)
    ap.add_argument('--nbridgeclients', action="store", type=int, dest="nbridgeclients", help="number N of total clients running with bridges for the generated topology", metavar='N', default=NBRIDGECLIENTS)
    ap.add_argument('--fweb', action="store", type=float, dest="fweb", help="fraction F of web client connections", metavar='F', default=FWEB)
    ap.add_argument('--fbulk', action="store", type=float, dest="fbulk", help="fraction F of bulk HTTP client connections", metavar='F', default=FBULK)
    ap.add_argument('--nperf50k', action="store", type=float, dest="nperf50k", help="number N of 50KiB perf clients", metavar='F', default=NPERF50K)
    ap.add_argument('--nperf1m', action="store", type=float, dest="nperf1m", help="number N of 1MiB perf clients", metavar='F', default=NPERF1M)
    ap.add_argument('--nperf5m', action="store", type=float, dest="nperf5m", help="number N of 5MiB perf clients", metavar='F', default=NPERF5M)
    ap.add_argument('--nservers', action="store", type=int, dest="nservers", help="number N of fileservers", metavar='N', default=NSERVERS)
    ap.add_argument('--geoippath', action="store", dest="geoippath", help="path to geoip file, needed to convert IPs to cluster codes", default=INSTALLPREFIX+"share/geoip")
    ap.add_argument('--seed', action="store", type=float, dest="seed", help="seed the random generator", metavar='S', default=SEED)

    # positional args (required)
    ap.add_argument('alexa', action="store", type=str, help="path to an ALEXA file (produced with contrib/parsealexa.py)", metavar='ALEXA', default=None)
    ap.add_argument('consensus', action="store", type=str, help="path to a current Tor CONSENSUS file", metavar='CONSENSUS', default=None)
    ap.add_argument('descriptors', action="store", type=str, help="path to top-level directory containing current Tor server-descriptors", metavar='DESCRIPTORS', default=None)
    ap.add_argument('extrainfos', action="store", type=str, help="path to top-level directory containing current Tor extra-infos", metavar='EXTRAINFOS', default=None)
    ap.add_argument('connectingusers', action="store", type=str, help="path to csv containing Tor directly connecting user country data", metavar='CONNECTINGUSERS', default=None)

    # get arguments, accessible with args.value
    args = ap.parse_args()

    random.seed(args.seed)

    totalclientf = args.fweb + args.fbulk
    if totalclientf != 1.0:
        log("client fractions do not add to 1.0! please fix arguments...")
        return

    # fixup paths from user
    args.prefix = os.path.abspath(os.path.expanduser(args.prefix))
    args.consensus = os.path.abspath(os.path.expanduser(args.consensus))
    args.alexa = os.path.abspath(os.path.expanduser(args.alexa))
    args.descriptors = os.path.abspath(os.path.expanduser(args.descriptors))
    args.extrainfos = os.path.abspath(os.path.expanduser(args.extrainfos))
    args.connectingusers = os.path.abspath(os.path.expanduser(args.connectingusers))
    args.geoippath = os.path.abspath(os.path.expanduser(args.geoippath))

    args.torbin = which("tor")
    args.torgencertbin = which("tor-gencert")
    if args.torbin is None or args.torgencertbin is None:
        log("please ensure 'tor' and 'tor-gencert' are in your 'PATH'")
        exit(-1)
    args.devnull = open("/dev/null", 'wb')

    generate(args)
    log("finished generating {0}/shadow.config.xml".format(os.getcwd()))

def getfp(args, torrc, name, datadir="."):
    """Run Tor with --list-fingerprint to get its fingerprint, read
    the fingerprint file and return the fingerprint. Uses current
    directory for DataDir. Returns a two-element list where the first
    element is an integer return code from running tor (0 for success)
    and the second is a string with the fingerprint, or None on
    failure."""
    listfp = "{0} --list-fingerprint --DataDirectory {2} --Nickname {3} -f {1}".format(args.torbin, torrc, datadir, name)
    retcode = subprocess.call(shlex.split(listfp), stdout=args.devnull, stderr=subprocess.STDOUT)
    if retcode !=0: return retcode, None
    fp = None
    with open("{0}/fingerprint".format(datadir), 'r') as f:
        fp = f.readline().strip().split()[1]
        fp = " ".join(fp[i:i+4] for i in range(0, len(fp), 4))
    return 0, fp

def generate(args):
    # get list of relays, sorted by increasing bandwidth
    validyear, validmonth, relays = parse_consensus(args.consensus)

    # separate out relays
    exitguards, exits, guards, middles = [], [], [], []
    for relay in relays:
        if relay.isExit and relay.isGuard: exitguards.append(relay)
        elif relay.isExit: exits.append(relay)
        elif relay.isGuard: guards.append(relay)
        else: middles.append(relay)

    geoentries = getGeoEntries(args.geoippath)

    # sample for the relays we'll use for our nodes
    n_exitguards = int(float(len(exitguards)) / float(len(relays)) * args.nrelays)
    n_guards = int(float(len(guards)) / float(len(relays)) * args.nrelays)
    n_exits = int(float(len(exits)) / float(len(relays)) * args.nrelays)
    n_middles = int(float(len(middles)) / float(len(relays)) * args.nrelays)

    exitguards_nodes = getRelays(exitguards, n_exitguards, geoentries, args.descriptors, args.extrainfos, validyear, validmonth)
    guards_nodes = getRelays(guards, n_guards, geoentries, args.descriptors, args.extrainfos, validyear, validmonth)
    exits_nodes = getRelays(exits, n_exits, geoentries, args.descriptors, args.extrainfos, validyear, validmonth)
    middles_nodes = getRelays(middles, n_middles, geoentries, args.descriptors, args.extrainfos, validyear, validmonth)

    # get the fastest nodes at the front
    exitguards_nodes.reverse()
    guards_nodes.reverse()
    exits_nodes.reverse()
    middles_nodes.reverse()

    servers = getServers(geoentries, args.alexa)
    clientCountryCodes = getClientCountryChoices(args.connectingusers)

    if not os.path.exists(newweb_conf_dir): os.makedirs(newweb_conf_dir)

    # output choices
    if not os.path.exists("conf"): os.makedirs("conf")
    with open("conf/relay.choices.csv", "wb") as f:
        print >>f, Relay.CSVHEADER
        for r in exitguards_nodes: print >>f, r.toCSV()
        for r in guards_nodes: print >>f, r.toCSV()
        for r in exits_nodes: print >>f, r.toCSV()
        for r in middles_nodes: print >>f, r.toCSV()

    # build the XML
    root = etree.Element("shadow")

    servernames = []
    i = 0
    country_to_webserver_names = defaultdict(set)
    while i < args.nservers:
        serverip, servercode = chooseServer(servers)
        i += 1
        name = "server{0}".format(i)
        servernames.append(name)
        e = etree.SubElement(root, "node")
        e.set("id", name)
        e.set("iphint", serverip)
        e.set("geocodehint", servercode)
        country_to_webserver_names[servercode.upper()].add(name)
        e.set("typehint", "server")
        e.set("bandwidthup", "102400") # in KiB
        e.set("bandwidthdown", "102400") # in KiB
        e.set("quantity", "1")
        e.set("cpufrequency", "10000000") # 10 GHz b/c we dont want bottlenecks
        a = etree.SubElement(e, "application")
        a.set("plugin", "tgen")
        a.set("starttime", "1")
        a.set("arguments", "conf/tgen.server.graphml.xml")

        a = etree.SubElement(e, "application")
        a.set("plugin", webserver_pluginid)
        a.set("starttime", "1")
        a.set("arguments", webserver_args)

        pass
    write_tgen_config_files(servernames)

    with open("conf/shadowresolv.conf", "wb") as f: print >>f, "nameserver 127.0.0.1"

    default_tor_args = "--Address ${NODEID} --Nickname ${NODEID} --DataDirectory shadow.data/hosts/${NODEID} --GeoIPFile "+INSTALLPREFIX+"share/geoip --defaults-torrc conf/tor.common.torrc"

    # tor directory authorities - choose the fastest relays (no authority is an exit node)
    dirauths = [] # [name, v3ident, fingerprint] for torrc files
    os.makedirs("shadow.data.template/hosts")
    os.chdir("shadow.data.template/hosts")

    os.makedirs("torflowauthority")
    v3bwfile = open("torflowauthority/v3bw.init.consensus", "wb")
    os.symlink("v3bw.init.consensus", "torflowauthority/v3bw")
    v3bwfile.write("1\n")

    guardnames, guardfps = [], []
    exitnames, exitfps = [], []

    with open("authgen.torrc", 'w') as fauthgen: print >>fauthgen, "DirServer test 127.0.0.1:5000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000\nORPort 5000\n"
    with open("authgen.pw", 'w') as fauthgenpw: print >>fauthgenpw, "shadowprivatenetwork\n"
    starttime = 5
    i = 1
    while i <= args.nauths:
        auth = []
        name = "4uthority{0}".format(i)
        auth.append(name)
        guardnames.append(name)

        # add to shadow hosts file
        #authority = guards_nodes.pop()
        #torargs = "{0} -f tor.authority.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, authority.getBWRateArg(), authority.getBWBurstArg()) # in bytes
        #addRelayToXML(root, starttime, torargs, None, None, name, authority.download, authority.upload, authority.ip, authority.code)
        authority = guards_nodes.pop()
        torargs = "{0} -f conf/tor.authority.torrc".format(default_tor_args)
        addRelayToXML(root, starttime, torargs, None, name, download=6400, upload=6400)

        # generate keys for tor
        os.makedirs(name)
        os.chdir(name)
        rc, fp = getfp(args, '../authgen.torrc', name)
        guardfps.append(fp)
        if rc != 0: return rc
        gencert = "{0} --create-identity-key -m 24 --passphrase-fd 0".format(args.torgencertbin)
        with open("../authgen.pw", 'r') as pwin: retcode = subprocess.call(shlex.split(gencert), stdin=pwin, stdout=args.devnull, stderr=subprocess.STDOUT)
        if retcode !=0: return retcode
        with open("authority_certificate", 'r') as f:
            for line in f:
                if 'fingerprint' in line: auth.append(line.strip().split()[1]) # v3ident
        v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), authority.getBWConsensusArg(), name))
        auth.append(fp)
        dirauths.append(auth)
        shutil.move("authority_certificate", "keys/.")
        shutil.move("authority_identity_key", "keys/.")
        shutil.move("authority_signing_key", "keys/.")
        os.chdir("..")
        starttime += 1
        i += 1

    # tor bridge authority
    bridgeauths = []                    # [name, None, fingerprint]
    if args.nbridgeauths:
        with open("bridgeauthgen.torrc", 'w') as fauthgen: print >>fauthgen, "DirServer test 127.0.0.1:5000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000\nORPort 5000\n"
        i = 1
        while i <= args.nbridgeauths:
            auth = []
            name = "4uthoritybridge{0}".format(i)
            guardnames.append(name)
            auth.append(name)
            auth.append(None)
            bridgeauthority = guards_nodes.pop()
            torargs = "{0} -f conf/tor.bridgeauthority.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, bridgeauthority.getBWRateArg(), bridgeauthority.getBWBurstArg()) # in bytes
            addRelayToXML(root, starttime, torargs, None, name, bridgeauthority.download, bridgeauthority.upload, bridgeauthority.ip, bridgeauthority.code)

            # generate certificate in order to get the fingerprint
            os.makedirs(name)
            os.chdir(name)
            rc, fp = getfp(args, '../bridgeauthgen.torrc', name)
            guardfps.append(fp)
            os.chdir("..")
            if rc != 0: return rc
            v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), bridgeauthority.getBWConsensusArg(), name))
            auth.append(fp)
            bridgeauths.append(auth)
            i += 1

    # boot relays equally spread out between 1 and 11 minutes
    secondsPerRelay = 600.0 / (len(exitguards_nodes) + len(exits_nodes) + len(guards_nodes) + len(middles_nodes))
    relayStartTime = 60.0 # minute 1

    # exitguard relays
    i = 1
    for r in exitguards_nodes:
        assert r.isExit is True
        assert r.isGuard is True
        name = "relayexitguard{0}".format(i)
        guardnames.append(name)
        exitnames.append(name)
        os.makedirs(name)
        os.chdir(name)
        rc, fp = getfp(args, '../authgen.torrc', name)
        guardfps.append(fp)
        exitfps.append(fp)
        if rc != 0: return rc
        os.chdir("..")
        v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), r.getBWConsensusArg(), name))
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "{0} -f conf/tor.exitguard.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, r.getBWRateArg(), r.getBWBurstArg()) # in bytes
        addRelayToXML(root, starttime, torargs, None, name, r.download, r.upload, r.ip, r.code,
                      tproxy_args=tproxy_ssp_args)
        relayStartTime += secondsPerRelay
        i += 1

    # guard relays
    i = 1
    for r in guards_nodes:
        assert r.isExit is not True
        assert r.isGuard is True
        name = "relayguard{0}".format(i)
        guardnames.append(name)
        os.makedirs(name)
        os.chdir(name)
        rc, fp = getfp(args, '../authgen.torrc', name)
        guardfps.append(fp)
        if rc != 0: return rc
        os.chdir("..")
        v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), r.getBWConsensusArg(), name))
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "{0} -f conf/tor.guard.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, r.getBWRateArg(), r.getBWBurstArg()) # in bytes
        addRelayToXML(root, starttime, torargs, None, name, r.download, r.upload, r.ip, r.code)
        relayStartTime += secondsPerRelay
        i += 1

    # exit relays
    i = 1
    for r in exits_nodes:
        assert r.isExit is True
        assert r.isGuard is not True
        name = "relayexit{0}".format(i)
        exitnames.append(name)
        os.makedirs(name)
        os.chdir(name)
        rc, fp = getfp(args, '../authgen.torrc', name)
        exitfps.append(fp)
        if rc != 0: return rc
        os.chdir("..")
        v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), r.getBWConsensusArg(), name))
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "{0} -f conf/tor.exit.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, r.getBWRateArg(), r.getBWBurstArg()) # in bytes
        addRelayToXML(root, starttime, torargs, None, name, r.download, r.upload, r.ip, r.code,
                      tproxy_args=tproxy_ssp_args)
        relayStartTime += secondsPerRelay
        i += 1

    # middle relays
    i = 1
    for r in middles_nodes:
        assert r.isExit is not True
        assert r.isGuard is not True
        name = "relaymiddle{0}".format(i)
        os.makedirs(name)
        os.chdir(name)
        rc, fp = getfp(args, '../authgen.torrc', name)
        if rc != 0: return rc
        os.chdir("..")
        v3bwfile.write("node_id=${0}\tbw={1}\tnick={2}\n".format(fp.replace(" ", ""), r.getBWConsensusArg(), name))
        starttime = "{0}".format(int(round(relayStartTime)))
        torargs = "{0} -f conf/tor.middle.torrc --BandwidthRate {1} --BandwidthBurst {2}".format(default_tor_args, r.getBWRateArg(), r.getBWBurstArg()) # in bytes
        addRelayToXML(root, starttime, torargs, None, name, r.download, r.upload, r.ip, r.code)
        relayStartTime += secondsPerRelay
        i += 1

    v3bwfile.close()
    os.chdir("../..") # move out of initdata dir

    # clients
    nbulkclients = int(args.fbulk * args.nclients)
    nwebclients = int(args.nclients - nbulkclients)
    nperf50kclients = int(args.nperf50k)
    nperf1mclients = int(args.nperf1m)
    nperf5mclients = int(args.nperf5m)

    # boot clients equally spread out between 15 and 25 minutes
    secondsPerClient = 600.0 / (nbulkclients+nwebclients+nperf50kclients+nperf1mclients+nperf5mclients)
    clientStartTime = 900.0 # minute 15

    # clients are separated into bulk/web downloaders who always download their file type
    i = 1
    while i <= 1 and False: # TODO enable when torflow works
        name = "torflowauthority"
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.client.torrc".format(default_tor_args) # in bytes

        addRelayToXML(root, starttime, torargs, None, name, download=1048576, upload=1048576, code=choice(clientCountryCodes), torflowworkers=max(int(args.nrelays/50), 1))

        clientStartTime += secondsPerClient
        i += 1

    i = 1

    webclient_names = set()
    while i <= nwebclients:
        name = "webclient{0}".format(i)
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.client.torrc".format(default_tor_args) # in bytes
        tgenargs = None

        webclient_names.add(name)

        addRelayToXML(root, starttime, torargs, tgenargs, name, code=choice(clientCountryCodes),
                      tproxy_args=tproxy_csp_args, is_newweb_client=True)

        clientStartTime += secondsPerClient
        i += 1

    i = 1
    while i <= nbulkclients:
        name = "bulkclient{0}".format(i)
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.client.torrc".format(default_tor_args) # in bytes
        tgenargs = "conf/tgen.torbulkclient.graphml.xml"

        addRelayToXML(root, starttime, torargs, tgenargs, name, code=choice(clientCountryCodes))

        clientStartTime += secondsPerClient
        i += 1

    i = 1
    while i <= nperf50kclients:
        name = "perf50kclient{0}".format(i)
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.torperf.torrc".format(default_tor_args) # in bytes
        tgenargs = "conf/tgen.torperf50kclient.graphml.xml"

        addRelayToXML(root, starttime, torargs, tgenargs, name, code=choice(clientCountryCodes))

        clientStartTime += secondsPerClient
        i += 1

    i = 1
    while i <= nperf1mclients:
        name = "perf1mclient{0}".format(i)
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.torperf.torrc".format(default_tor_args) # in bytes
        tgenargs = "conf/tgen.torperf1mclient.graphml.xml"

        addRelayToXML(root, starttime, torargs, tgenargs, name, code=choice(clientCountryCodes))

        clientStartTime += secondsPerClient
        i += 1

    i = 1
    while i <= nperf5mclients:
        name = "perf5mclient{0}".format(i)
        starttime = "{0}".format(int(round(clientStartTime)))
        torargs = "{0} -f conf/tor.torperf.torrc".format(default_tor_args) # in bytes
        tgenargs = "conf/tgen.torperf5mclient.graphml.xml"

        addRelayToXML(root, starttime, torargs, tgenargs, name, code=choice(clientCountryCodes))

        clientStartTime += secondsPerClient
        i += 1

    # generate torrc files now that we know the authorities and bridges
    bridges = None
    write_torrc_files(args, dirauths, bridgeauths, bridges, guardfps, exitfps)

    write_newweb_config()

    # write the country_to_webserver_names file
    with open('countrycode_to_webserver_names.json', 'w') as fp:
        import json
        output_dict = {}
        for country, server_names in country_to_webserver_names.iteritems():
            output_dict[country] = sorted(server_names)
            pass
        json.dump(output_dict, fp, indent=2, sort_keys=True)
        pass

    with open('webclient_names.json', 'w') as fp:
        import json
        json.dump(sorted(list(webclient_names)), fp, indent=2, sort_keys=True)
        pass

    # finally, print the XML file
    with open("shadow.config.xml", 'wb') as fhosts:
        # plug-ins

        e = etree.Element("plugin")
        e.set("id", "tgen")
        e.set("path", "{0}plugins/libshadow-plugin-tgen.so".format(INSTALLPREFIX))
        root.insert(0, e)

        # TODO enable when torflow works
        #e = etree.Element("plugin")
        #e.set("id", "torflow")
        #e.set("path", "{0}plugins/libshadow-plugin-torflow.so".format(INSTALLPREFIX))
        #root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", "torctl")
        e.set("path", "{0}plugins/libshadow-plugin-torctl.so".format(INSTALLPREFIX))
        root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", "tor")
        e.set("path", "{0}plugins/libshadow-plugin-tor.so".format(INSTALLPREFIX))
        root.insert(0, e)


        e = etree.Element("plugin")
        e.set("id", ioservice_pluginid)
        e.set("path", "{0}plugins/libshadow-plugin-io_process.so".format(INSTALLPREFIX))
        root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", renderer_pluginid)
        e.set("path", "{0}plugins/libshadow-plugin-render_process.so".format(INSTALLPREFIX))
        root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", tproxy_pluginid)
        e.set("path", "{0}plugins/libshadow-plugin-transport_proxy.so".format(INSTALLPREFIX))
        root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", driver_pluginid)
        e.set("path", "{0}plugins/libshadow-plugin-driver_process.so".format(INSTALLPREFIX))
        root.insert(0, e)

        e = etree.Element("plugin")
        e.set("id", webserver_pluginid)
        e.set("path", "{0}plugins/libshadow-plugin-webserver.so".format(INSTALLPREFIX))
        root.insert(0, e)



        # internet topology map
        e = etree.Element("topology")
        e.set("path", "{0}share/topology.graphml.xml".format(INSTALLPREFIX))
        root.insert(0, e)

        # kill time
        e = etree.Element("kill")
        e.set("time", "3600")
        root.insert(0, e)

        root.insert(0, etree.Comment(' generated with cmd: %s ' % (' '.join(sys.argv))))

        # all our hosts
        print >>fhosts, (etree.tostring(root, pretty_print=True, xml_declaration=False))

def addRelayToXML(root, starttime, torargs, tgenargs, name, download=0, upload=0, ip=None, code=None, torflowworkers=1, tproxy_args=None, is_newweb_client=False): # bandwidth in KiB
    # node
    e = etree.SubElement(root, "node")
    e.set("id", name)
    if ip is not None and ip != "127.0.0.1": e.set("iphint", ip)
    if code is not None: e.set("geocodehint", code)

    if 'relay' in name or '4uthority' in name: e.set("typehint", "relay")
    elif 'client' in name: e.set("typehint", "client")

    # bandwidth is optional in XML, will be assigned based on cluster if not given
    if download > 0: e.set("bandwidthdown", "{0}".format(download)) # in KiB
    if upload > 0: e.set("bandwidthup", "{0}".format(upload)) # in KiB

    e.set("quantity", "1")
    e.set("cpufrequency", choice(CPUFREQS))

    # applications - wait 5 minutes to start applications
    if torargs is not None:
        if "torflowauthority" in name:
            ports, servers = [], []
#            for i in xrange(torflowworkers):
#                serverport, controlport, socksport = 10000+i, 11000+i, 12000+i
#                ports.append("{0}:{1}".format(socksport, controlport))
#                servers.append("torflowauthority:{0}".format(serverport))
#                a = etree.SubElement(e, "application")
#                a.set("plugin", "filetransfer")
#                a.set("starttime", "{0}".format(int(starttime)))
#                a.set("arguments", "server {0} ~/.shadow/share/".format(serverport))
#                a = etree.SubElement(e, "application")
#                a.set("plugin", "tor")
#                a.set("starttime", "{0}".format(int(starttime)))
#                mytorargs = torargs.replace("--DataDirectory ./data/${NODEID}", "--DataDirectory ./data/${NODEID}-worker"+str(i))
#                mytorargs = "{0} --ControlPort {1} --SocksPort {2} --SocksTimeout 5".format(mytorargs, controlport, socksport)
#                a.set("arguments", mytorargs)
#                a = etree.SubElement(e, "application")
#                a.set("plugin", "torctl")
#                a.set("starttime", "{0}".format(int(starttime)+1))
#                a.set("arguments", "localhost {0} STREAM,CIRC,CIRC_MINOR,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW,BUILDTIMEOUT_SET,CLIENTS_SEEN,GUARD,CELL_STATS,TB_EMPTY".format(controlport))
            serverport, controlport, socksport = 8080, 9050, 9000
            a = etree.SubElement(e, "application")
            a.set("plugin", "filetransfer")
            a.set("starttime", "{0}".format(int(starttime)))
            a.set("arguments", "server {0} ~/.shadow/share/".format(serverport))
            a = etree.SubElement(e, "application")
            a.set("plugin", "tor")
            a.set("starttime", "{0}".format(int(starttime)))
            a.set("arguments", "{0} --ControlPort {1} --SocksPort {2} --SocksTimeout 5".format(torargs, controlport, socksport))
            a = etree.SubElement(e, "application")
            a.set("plugin", "torctl")
            a.set("starttime", "{0}".format(int(starttime)+1))
            a.set("arguments", "localhost {0} STREAM,CIRC,CIRC_MINOR,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW,BUILDTIMEOUT_SET,CLIENTS_SEEN,GUARD,CELL_STATS,TB_EMPTY".format(controlport))
            a = etree.SubElement(e, "application")
            a.set("plugin", "torflow")
            a.set("starttime", "{0}".format(int(starttime)+60))
            a.set("arguments", "shadow.data/hosts/torflowauthority/v3bw 60 {0} 50 0.5 {1} {2} torflowauthority:{3}".format(torflowworkers, controlport, socksport, serverport))
        else:
            a = etree.SubElement(e, "application")
            a.set("plugin", "tor")
            a.set("starttime", "{0}".format(int(starttime)))
            a.set("arguments", torargs)
            a = etree.SubElement(e, "application")
            a.set("plugin", "torctl")
            a.set("starttime", "{0}".format(int(starttime)+1))
            a.set("arguments", "localhost 9051 STREAM,CIRC,CIRC_MINOR,ORCONN,BW,STREAM_BW,CIRC_BW,CONN_BW,BUILDTIMEOUT_SET,CLIENTS_SEEN,GUARD,CELL_STATS,TB_EMPTY")
    if tgenargs is not None:
        a = etree.SubElement(e, "application")
        a.set("plugin", "tgen")
        a.set("starttime", "{0}".format(int(starttime)+300))
        a.set("arguments", tgenargs)
        pass

    if tproxy_args is not None:
        a = etree.SubElement(e, "application")
        a.set("plugin", "tproxy")
        a.set("starttime", "{0}".format(int(starttime)))
        a.set("arguments", tproxy_args)
        pass

    if is_newweb_client:
        assert tproxy_args is not None

        # add the browser-related plugins

        for (plugin_id, plugin_starttime, plugin_args) in (
                (ioservice_pluginid, int(starttime)+300, ioservice_args),
                (renderer_pluginid, int(starttime)+301, renderer_args),
                (driver_pluginid, int(starttime)+302, driver_args),
                ):
            a = etree.SubElement(e, "application")
            a.set("plugin", plugin_id)
            a.set("starttime", "{0}".format(int(plugin_starttime)))
            a.set("arguments", plugin_args)
            pass
        pass

    pass

def getClientCountryChoices(connectinguserspath):
    lines = None
    with open(connectinguserspath, 'rb') as f:
        lines = f.readlines()

    assert len(lines) > 11
    header = lines[0].strip().split(',')

    total = 0
    counts = dict()
    dates_seen = 0          #used to get the data for the last 10 dates recorded
    last_date_seen = ''
    linei = 0
    while True:
        linei -= 1                               #get next line above

        line = lines[linei]
        parts = line.strip().split(',')

        if parts[0] != last_date_seen:
            dates_seen += 1
            last_date_seen = parts[0]
            if dates_seen > 10:
                break                            #if last 10 dates are analyzed, we're finished

        if parts[2] != "??" and parts[2] != "":  #parts[2] == country
            country = parts[2]
            n = int(parts[7])                    #parts[7] == num of clients
            total += n
            if country not in counts: counts[country] = 0
            counts[country] += n


    codes = []
    for c in counts:
        frac = float(counts[c]) / float(total)
        n = int(frac * 1000)

        code = c.upper()
#        if code == "US" or code == "A1" or code == "A2": code = "USMN"
        code = "{0}".format(code)

        for i in xrange(n):
            codes.append(code)

    return codes

def ip2long(ip):
    """
    Convert a IPv4 address into a 32-bit integer.

    @param ip: quad-dotted IPv4 address
    @type ip: str
    @return: network byte order 32-bit integer
    @rtype: int
    """
    ip_array = ip.split('.')
    ip_long = long(ip_array[0]) * 16777216 + long(ip_array[1]) * 65536 + long(ip_array[2]) * 256 + long(ip_array[3])
    return ip_long

def getClusterCode(geoentries, ip):
    # use geoip entries to find our cluster code for IP
    ipnum = ip2long(ip)
    #print "{0} {1}".format(ip, ipnum)
    # theres probably a faster way of doing this, but this is python and i dont care
    for entry in geoentries:
        if ipnum >= entry.lownum and ipnum <= entry.highnum:
#            if entry.countrycode == "US": return "USMN" # we have no USUS code (USMN gets USCENTRAL)
            return "{0}".format(entry.countrycode)
    log("Warning: Cant find code for IP '{0}' Num '{1}', defaulting to 'US'".format(ip, ipnum))
    return "US"

def getGeoEntries(geoippath):
    entries = []
    with open(geoippath, "rb") as f:
        for line in f:
            if line[0] == "#": continue
            parts = line.strip().split(',')
            entry = GeoIPEntry(parts[0], parts[1], parts[2])
            entries.append(entry)
    return entries

def getServers(geoentries, alexapath):
    # return IPs from args.alexa, keeping sort order
    servers = {}
    servers['allips'] = []
    servers['codes'] = {}
    servers['iptocode'] = {}

    with open(alexapath, 'rb') as f:
        for line in f:
            parts = line.strip().split(',')
            ip = parts[2]
            if ip == "127.0.0.1": continue
            servers['allips'].append(ip)

            code = getClusterCode(geoentries, ip)
            servers['iptocode'][ip] = code

            if code not in servers['codes']:
                servers['codes'][code] = {}
                servers['codes'][code]['ips'] = []
                servers['codes'][code]['index'] = 0

            servers['codes'][code]['ips'].append(ip)

    return servers

def chooseServer(servers):
    # first get a random code
    tempip = choice(servers['allips'])
    code = servers['iptocode'][tempip]

    # now we have our code, get the next index in this code's list
    s = servers['codes'][code]
    i = s['index'] % (len(s['ips']))
    ip = s['ips'][i]
    s['index'] += 1

    return ip, code

def getRelays(relays, k, geoentries, descriptorpath, extrainfopath, validyear, validmonth):
    sample = sample_relays(relays, k)

    # maps for easy relay lookup while parsing descriptors
    ipmap, fpmap = dict(), dict()
    for s in sample:
        if s.ip not in ipmap:
            ipmap[s.ip] = s

    # go through all the descriptors and find the bandwidth rate, burst, and
    # history from the most recent descriptor of each relay in our sample
    for root, _, files in os.walk(descriptorpath):
        for filename in files:
            fullpath = os.path.join(root, filename)
            with open(fullpath, 'rb') as f:
                rate, burst, observed = 0, 0, 0
                ip = ""
                fingerprint = None
                published = None

                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 0: continue
                    if parts[0] == "router":
                        ip = parts[2]
                        if ip not in ipmap: break
                    elif parts[0] == "published":
                        published = "{0} {1}".format(parts[1], parts[2])
                    elif parts[0] == "bandwidth":
                        rate, burst, observed = int(parts[1]), int(parts[2]), int(parts[3])
                    elif parts[0] == "opt" and parts[1] == "fingerprint":
                        fingerprint = "".join(parts[2:])

                if ip not in ipmap: continue

                relay = ipmap[ip]
                # we want to know about every fingerprint
                if fingerprint is not None: fpmap[fingerprint] = relay
                # we want to know every observed bandwidth to est. link speed
                relay.setMaxObserved(observed)
                # we only want the latest rate and burst settings
                if published is not None:
                    datet = datetime.strptime(published, "%Y-%m-%d %H:%M:%S")
                    unixt = time.mktime(datet.timetuple())
                    relay.setLimits(rate, burst, unixt)

    # now check for extra info docs for our chosen relays, so we get read and write histories
    # here the published time doesnt matter b/c we are trying to estimate the
    # relay's ISP link speed
    for root, _, files in os.walk(extrainfopath):
        for filename in files:
            fullpath = os.path.join(root, filename)
            with open(fullpath, 'rb') as f:
                maxwrite, maxread = 0, 0
                totalwrite, totalread = 0, 0
                fingerprint = None
                published = None

                for line in f:
                    parts = line.strip().split()
                    if len(parts) == 0: continue
                    if parts[0] == "extra-info":
                        if len(parts) < 3: break # cant continue if we dont know the relay
                        fingerprint = parts[2]
                        if fingerprint not in fpmap: break
                    elif parts[0] == "published":
                        # only count data from our modeled month towards our totals
                        published = "{0} {1}".format(parts[1], parts[2])
                        datet = datetime.strptime(published, "%Y-%m-%d %H:%M:%S")
                        if datet.year != validyear or datet.month != validmonth:
                            published = None
                    elif parts[0] == "write-history":
                        if len(parts) < 6: continue # see if we can get other info from this doc
                        seconds = float(int(parts[3][1:]))
                        speeds = parts[5]
                        nbytes = speeds.split(',')
                        maxwrite = int(max([int(i) for i in nbytes]) / seconds)
                        totalwrite = int(float(sum([int(i) for i in nbytes])) / float(seconds*len(nbytes)))
                    elif parts[0] == "read-history":
                        if len(parts) < 6: continue # see if we can get other info from this doc
                        seconds = float(int(parts[3][1:]))
                        speeds = parts[5]
                        nbytes = speeds.split(',')
                        maxread = int(max([int(i) for i in nbytes]) / seconds)
                        totalread = int(float(sum([int(i) for i in nbytes])) / float(seconds*len(nbytes)))

                if fingerprint is not None and fingerprint in fpmap:
                    relay = fpmap[fingerprint]
                    relay.setMaxSpeeds(maxread, maxwrite)
                    if published is not None: relay.rates.append(totalread + totalwrite)

    # make sure we found some info for all of them, otherwise use defaults
    for s in sample:
        s.setRegionCode(getClusterCode(geoentries, s.ip))
        s.computeSpeeds()

    return sample

def sample_relays(relays, k):
    """
    sample k of n relays
    split list into k buckts, take the median from each bucket
    this provides the statistically best fit to the original list

    relays list should be sorted by bandwidth
    """
    n = len(relays)
    if k >= n:
        k = n
        print "warning: choosing only {0} of {1} relays".format(k, n)
    assert k <= n

    t = 0
    bins = []
    for _ in range(k):
        abin = []
        for _ in range(n/k):
            abin.append(relays[t])
            t += 1
        bins.append(abin)

    sample = []
    for abin in bins: sample.append(abin[len(abin)/2])

    return sample

def parse_consensus(consensus_path):
    relays = []
    ip = ""
    bw = 0.0
    isExit = False
    isGuard = False

    validyear, validmonth = None, None

    with open(consensus_path) as f:
        for line in f:
            if validyear == None and "valid-after" in line:
                parts = line.strip().split()
                dates = parts[1].split('-')
                validyear, validmonth = int(dates[0]), int(dates[1])
            elif line[0:2] == "r ":
                # append the relay that we just built up
                if ip != "":
                    r = Relay(ip, bw, isExit, isGuard)
                    relays.append(r)
                # reset for the next relay
                bw = 0.0
                isExit = False
                isGuard = False
                ip = line.strip().split()[6]
            elif line[0:2] == "s ":
                if " Exit " in line: isExit = True
                if " Guard " in line: isGuard = True
            elif line[0:2] == "w ":
                bw = float(line.strip().split()[1].split("=")[1]) # KiB

    return validyear, validmonth, sorted(relays, key=lambda relay: relay.getBWConsensusArg())

def write_torrc_files(args, dirauths, bridgeauths, bridges, guardids, exitids):
    auths_lines = ""
    # If we're running a bridge authority too, use
    # 'AlternateDirAuthority' together with 'AlternateBridgeAuthority'
    # instead of 'DirServer'.
    dirauthkw = 'DirServer'
    if len(bridgeauths) > 0:
        dirauthkw = 'AlternateDirAuthority'
    for auth in dirauths:
        auths_lines += "{3} {4} v3ident={0} orport=9111 {1}:9112 {2}\n".format(auth[1], auth[0], auth[2], dirauthkw, auth[0])
    for auth in bridgeauths:
        auths_lines += "AlternateBridgeAuthority {4} orport=9111 bridge {1}:9112 {2}\n".format(None, auth[0], auth[2], None, auth[0])
    bridges_lines = ""
    '''FIXME
    for bridge in bridges:
        bridges_lines += "Bridge {0}:9111\n".format(bridge[fixme])
    '''
    auth_name_csv = ','.join([auth[0] for auth in dirauths])
    common = \
'{0}\
TestingTorNetwork 1\n\
AllowInvalidNodes "entry,middle,exit,introduction,rendezvous"\n\
ServerDNSResolvConfFile conf/shadowresolv.conf\n\
ServerDNSTestAddresses {1}\n\
ServerDNSAllowBrokenConfig 1\n\
ServerDNSDetectHijacking 0\n\
NumCPUs 1\n\
Log notice stdout\n\
SafeLogging 0\n\
WarnUnsafeSocks 0\n\
ContactInfo shadow-support@cs.umn.edu\n\
DynamicDHGroups 0\n\
DisableDebuggerAttachment 0\n\
CellStatistics 1\n\
DirReqStatistics 1\n\
EntryStatistics 1\n\
ExitPortStatistics 1\n\
ExtraInfoStatistics 1\n\
CircuitPriorityHalflife 30\n\
ControlPort 9051\n'.format(auths_lines, auth_name_csv)
    clients = \
'ORPort 0\n\
DirPort 0\n\
ClientOnly 1\n\
SocksPort 9000\n\
SocksListenAddress 127.0.0.1\n\
BandwidthRate 5120000\n\
BandwidthBurst 10240000\n'
    bridgeclients = bridges_lines
    relays = \
'ORPort 9111\n\
SocksPort 0\n' # note - also need exit policy
    bridges = \
'BridgeRelay 1\n'
    guard_flag_str = ",".join([g.replace(" ", "") for g in guardids])
    exit_flag_str = ",".join([e.replace(" ", "") for e in exitids])
    authorities = \
'AuthoritativeDirectory 1\n\
V3AuthoritativeDirectory 1\n\
ORPort 9111\n\
DirPort 9112\n\
SocksPort 0\n\
V3BandwidthsFile shadow.data/hosts/torflowauthority/v3bw\n\
TestingDirAuthVoteGuard {0}\n\
TestingDirAuthVoteGuardIsStrict 1\n\
TestingDirAuthVoteExit {1}\n\
TestingDirAuthVoteExitIsStrict 1\n'.format(guard_flag_str, exit_flag_str)
    bridgeauths = \
'AuthoritativeDirectory 1\n\
BridgeAuthoritativeDir 1\n\
ORPort 9111\n\
DirPort 9112\n\
SocksPort 0\n' # note - also need exit policy
    dirserv = \
'DirPort 9112\n'
    epreject = 'ExitPolicy "reject *:*"\n'
    epaccept = 'ExitPolicy "accept *:*"\n'
    maxdirty = 'MaxCircuitDirtiness 10 seconds\n'
    noguards = 'UseEntryGuards 0\n'

    with open("conf/tor.common.torrc", 'wb') as f: print >>f, common
    with open("conf/tor.authority.torrc", 'wb') as f: print >>f, authorities + epreject
    if args.nbridgeauths > 0:
        with open("conf/tor.bridgeauthority.torrc", 'wb') as f: print >>f, bridgeauths + epreject
    with open("conf/tor.exitguard.torrc", 'wb') as f: print >>f, relays + dirserv + epaccept
    with open("conf/tor.guard.torrc", 'wb') as f: print >>f, relays + dirserv + epreject
    with open("conf/tor.exit.torrc", 'wb') as f: print >>f, relays + dirserv + epaccept
    with open("conf/tor.middle.torrc", 'wb') as f: print >>f, relays + dirserv + epreject
    if args.nbridges > 0:
        with open("conf/tor.bridge.torrc", 'wb') as f: print >>f, relays + epreject
    with open("conf/tor.client.torrc", 'wb') as f: print >>f, clients
    if args.nbridgeclients > 0:
        with open("conf/tor.bridgeclient.torrc", 'wb') as f: print >>f, clients + bridgeclients
    with open("conf/tor.torperf.torrc", 'wb') as f: print >>f, clients + maxdirty + noguards
    log("finished generating torrc files")

def write_newweb_config():
    tproxy_ssp_port = 3000

    tproxy_csp_socks_port = 2000
    tproxy_csp_ipc_port = 2100

    tamaraw_pkt_intvl = 5
    tamaraw_L = 100

    tor_socks_port = 9000

    renderer_ipc_port = 3000

    ioservice_ipc_port = 4000

    browser_proxy_mode_spec_fpath = 'browser_proxy_mode_spec.txt'
    page_models_list_fpath = 'page_models_list.txt'

    ioservice_conf = '''
--ioservice-ipc-port={ioservice_ipc_port}
--tproxy-socks-port={tproxy_csp_socks_port}
--tor-socks-port={tor_socks_port}

--browser-proxy-mode-spec-file={browser_proxy_mode_spec_file}
'''.format(ioservice_ipc_port=ioservice_ipc_port,
           tproxy_csp_socks_port=tproxy_csp_socks_port,
           tor_socks_port=9000,
           browser_proxy_mode_spec_file=browser_proxy_mode_spec_fpath,
           )
    with open(ioservice_conf_fpath, 'w') as f:
        print >>f, ioservice_conf
        pass

    renderer_conf = '''
--ioservice-ipc-port={ioservice_ipc_port}
--renderer-ipc-port={renderer_ipc_port}
'''.format(ioservice_ipc_port=ioservice_ipc_port,
           renderer_ipc_port=renderer_ipc_port)
    with open(renderer_conf_fpath, 'w') as f:
        print >>f, renderer_conf
        pass

    tproxy_ssp_conf = '''
--port={tproxy_ssp_port}

--tamaraw-packet-interval={tamaraw_pkt_intvl}
--tamaraw-L={tamaraw_L}
'''.format(tproxy_ssp_port=tproxy_ssp_port,
           tamaraw_pkt_intvl=tamaraw_pkt_intvl,
           tamaraw_L=tamaraw_L,
           )
    with open(tproxy_ssp_conf_fpath, 'w') as f:
        print >>f, tproxy_ssp_conf
        pass

    tproxy_csp_conf = '''
--port={tproxy_csp_socks_port}
--tproxy-ipc-port={tproxy_csp_ipc_port}
--ssp=127.0.0.1:{tproxy_ssp_port}
--tor-socks-port={tor_socks_port}

--browser-proxy-mode-spec-file={browser_proxy_mode_spec_file}

--tamaraw-packet-interval={tamaraw_pkt_intvl}
--tamaraw-L={tamaraw_L}
'''.format(tor_socks_port=9000,
           tproxy_ssp_port=tproxy_ssp_port,
           tproxy_csp_socks_port=tproxy_csp_socks_port,
           tproxy_csp_ipc_port=tproxy_csp_ipc_port,
           tamaraw_pkt_intvl=tamaraw_pkt_intvl,
           tamaraw_L=tamaraw_L,
           browser_proxy_mode_spec_file=browser_proxy_mode_spec_fpath,
           )
    with open(tproxy_csp_conf_fpath, 'w') as f:
        print >>f, tproxy_csp_conf
        pass

    driver_conf = '''
--ioservice-ipc-port={ioservice_ipc_port}
--renderer-ipc-port={renderer_ipc_port}
--tproxy-ipc-port={tproxy_csp_ipc_port}

--browser-proxy-mode-spec-file={browser_proxy_mode_spec_file}

--page-models-list-file={page_models_list_file}
'''.format(ioservice_ipc_port=ioservice_ipc_port,
           renderer_ipc_port=renderer_ipc_port,
           browser_proxy_mode_spec_file=browser_proxy_mode_spec_fpath,
           tproxy_csp_ipc_port=tproxy_csp_ipc_port,
           page_models_list_file=page_models_list_fpath,
           )
    with open(driver_conf_fpath, 'w') as f:
        print >>f, driver_conf
        pass

    pass

def write_tgen_config_files(servernames):
    servers = []
    for n in servernames: servers.append("{0}:{1}".format(n, TGEN_SERVERPORT))
    s = ','.join(servers)

    generate_tgen_server()
    generate_tgen_filetransfer_clients(servers=s)
    generate_tgen_perf_clients(servers=s, size="50 KiB", name="conf/tgen.torperf50kclient.graphml.xml")
    generate_tgen_perf_clients(servers=s, size="1 MiB", name="conf/tgen.torperf1mclient.graphml.xml")
    generate_tgen_perf_clients(servers=s, size="5 MiB", name="conf/tgen.torperf5mclient.graphml.xml")

def generate_tgen_server():
    G = DiGraph()
    G.add_node("start", serverport="{0}".format(TGEN_SERVERPORT))
    write_graphml(G, "conf/tgen.server.graphml.xml")

def generate_tgen_filetransfer_clients(servers):
    # webclients
    G = DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size="320 KiB")
    G.add_node("pause", time="1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "pause")
    G.add_edge("pause", "start")

    write_graphml(G, "conf/tgen.torwebclient.graphml.xml")

    # bulkclients
    G = DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size="5 MiB")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "start")

    write_graphml(G, "conf/tgen.torbulkclient.graphml.xml")

def generate_tgen_perf_clients(servers="server1:8888,server2:8888", size="50 KiB", name="conf/tgen.perf50kclient.graphml.xml"):
    G = DiGraph()

    G.add_node("start", socksproxy="localhost:9000", serverport="8888", peers=servers)
    G.add_node("transfer", type="get", protocol="tcp", size=size)
    G.add_node("pause", time="60")

    G.add_edge("start", "transfer")
    G.add_edge("transfer", "pause")
    G.add_edge("pause", "start")

    write_graphml(G, name)

## helper - test if program is in path
def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, _ = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None

def log(msg):
    color_start_code = "\033[94m" # red: \033[91m"
    color_end_code = "\033[0m"
    prefix = "[" + str(datetime.now()) + "] "
    print >> sys.stderr, color_start_code + prefix + msg + color_end_code

if __name__ == '__main__':
    main()
