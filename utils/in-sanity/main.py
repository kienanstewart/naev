#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# vim:set shiftwidth=4 tabstop=4 expandtab textwidth=80:
"""
A sanity check tool to know where the crap is.

For usage information, run ``main.py --usage``

For licensing information, see the LICENSE file in this directory.
"""

import os, sys
from argparse import ArgumentParser
import re
from types import *

__version__="0.1.1"

def lineNumber(string, start):
    """
    Return the line number and offset from a regex match
    """
    lineno = string.count('\n', 0, start) + 1
    offset = start - string.rfind('\n', 0, start)
    return (lineno, offset)

class sanitizer:

    _errorstring = "Can not found element ``%(content)s'' for function "\
                   "%(func)s at line %(lineno)d offset %(offset)d"

    def __init__(self, **args):
        """
        needs to be coded when I know how this class will be called
        """
        self.config = args
        self.luaScripts = list()

        if 'datpath' not in self.config.keys():
            self.config['datpath'] = os.path.join(self.config['basepath'],
                                                  'dat/')

        if self.config['use'] == 'missionxml':
            self.dirtyfiles_from_xml()
        elif self.config['use'] == 'rawfiles':
            self.dirtyfiles_from_directory()

        print('Compiling script files...',end='      ')
        for root, dir, files in os.walk(self.config['basepath']+'/scripts/'):
            self.addLuaFile(files, root)
        print('DONE')

        print('Compiling events files...',end='     ')
        for root, dir, files in os.walk(self.config['basepath']+'/dat/events/'):
            self.addLuaFile(files, root)
        print('DONE')

    def addLuaFile(self, files, root):
        for filename in files:
            if filename[-3:] == "lua":
                realname = os.path.join(root, filename)
                self.luaScripts.append(realname)

    def dirtyfiles_from_directory(self):
        """
        retrieve a list of files from the directory (the wild viking way)
        """
        print('Compiling file list like a wild viking ...', end='       ')
        for root, dir, files in os.walk(self.config['missionpath']):
            self.addLuaFile(files, root)
        print('DONE')
        return True

    def dirtyfiles_from_xml(self):
        """
        retrieve a list of files from the mission.xml (the quiet british way)
        """
        import xml.etree.ElementTree as ET
        file = os.path.join(self.config['basepath'],'dat/mission.xml')
        missionxml = ET.parse(file)
        print('Compiling file list ...', end='      ')
        for mission in missionxml.findall('mission'):
            for lf in mission.findall('lua'):
                lf = os.path.join(self.config['missionpath'], lf.text + '.lua')
                self.luaScripts.append(lf)
        print("DONE")
        return True

    def dah_doctor(self):
        """
        That's the doctor, it detect wrong stuff but can never heal you.
        """
        from readers import fleet
        from readers import ship
        from readers import outfit
        from readers import unidiff
        from readers.preprocessing import tech

        # This will do some preprocessing check in the xml files
        print('Preprocess valitation :')
        otech = tech(**self.config)

        # TODO: must be called when needed
        fleetdata = fleet(datpath=self.config['datpath'],
                        verbose=self.config['verbose'])
        shipdata = ship(datpath=self.config['datpath'],
                        verbose=self.config['verbose'],
                        tech=otech, fleetobj=fleetdata)
        outfitdata = outfit(datpath=self.config['datpath'],
                        verbose=self.config['verbose'], tech=otech)
        udata = unidiff(datpath=self.config['datpath'],
                        verbose=self.config['verbose'])
        rawstr = r"""
        (?P<func>
            pilot\.add\(|
            player\.addShip\(|
            addOutfit\(|
            diff\.apply\(
        )"(?P<content>[^"]+)"""

        search_cobj = re.compile(rawstr, re.VERBOSE| re.UNICODE)

        entry = dict()
        errors = list()

        # XXX This variable will stock all the lua files. This could lead to a
        # massive memory consumption.
        line = dict()

        print("Blind check now ...")
        for file in self.luaScripts:
            if self.config['verbose']:
                print("Processing file {0}...".format(file), end='       ')
            if len(errors) > 0:
                for error in errors:
                    print(error, file=sys.stderr)
                errors = list()

            try:
                line[file] = open(file, 'rU').read()
                this=False
                for match in search_cobj.finditer(line[file]):
                    lineno, offset = lineNumber(line[file], match.start())
                    info = dict(
                            lineno=lineno,
                            offset=offset,
                            func=match.group('func')[:-1],
                            content= match.group('content')
                    )

                    if info['func'] == 'pilot.add':
                        if not fleetdata.find(info['content']):
                            this = True
                    if info['func'] == 'addOutfit':
                        if not outfitdata.find(info['content']):
                            this = True
                    if info['func'] == 'player.addShip':
                        if not shipdata.find(info['content']):
                            this = True
                    if info['func'] == 'diff.apply':
                        if not udata.find(info['content']):
                            this = True

                    if this:
                        errors.append(self._errorstring % info)
                        this=False

            except IOError as error:
                print("I/O error: {0}".format(error), file=sys.stderr)
            except:
                raise

            if self.config['verbose']:
                print("DONE")

        print('Verifying ...')
        unused_data = dict()

        # makes sure that only category with unused stuff get listed
        tocheck = ((fleetdata, 'fleet'), (udata,'unidiff'),
                   (shipdata, 'ship'), (outfitdata, 'outfit'))
        for obj, key in tocheck:
            tmp = obj.get_unused()
            if len(tmp) > 0:
                unused_data.update({key: tmp})
        del(tmp)

        if len(unused_data) > 0:
            mcobj = dict()
            for (category, data) in unused_data.items():
                regex = r"""
                (?P<{0}>%s)
                """.format(category)
                rname = ''
                uniqList = list()
                for name in data:
                    name = name.replace(' ', "\s")
                    rname = rname + name + '|'
                    if name not in uniqList:
                        uniqList.append(name)
                regex = regex % rname[:-1]
                mcobj.update({category: re.compile(regex, re.VERBOSE| re.UNICODE)})

            for (file, content) in line.items():
                for (category, cobj) in mcobj.items():
                    for match in cobj.finditer(content):
                        groups = match.groupdict()
                        for obj, key in tocheck:
                            if key == category:
                                obj.set_unknown(groups[category])

        outfitdata.showMissingTech()
        shipdata.showMissingTech()

        if self.config['show_unused']:
            # XXX there is no real needs to use these two because of
            # ''showMissingTech`` called earlier, unless something is tagged as
            # 'UNKNOWN'
            outfitdata.show_unused()
            shipdata.show_unused()
            fleetdata.show_unused()
            udata.show_unused()

if __name__ == "__main__":

    parser = ArgumentParser(description="""
                Naev sanity check v%s.
                Please, don't go insane.
             """ %  __version__)
    parser.add_argument('--use', '-u',
                        choices=['missionxml','rawfiles'], default='missionxml',
                        help="""
                        Use the rawfiles option to run into the directory like
                        a wild viking. Otherwise, the script will use the active
                        mission list to load the file list.
                        """)

    gfleet = parser.add_argument_group('Fleets')
    gfleet.add_argument('--show-unused', action='store_true', default=False,
                        help='Show unused fleets from the xml files')

    parser.add_argument('--version', action='version',
                        version='%(prog)s '+__version__)
    parser.add_argument('--verbose', action='store_true', default=False)
    parser.add_argument('basepath',
                        help='Path to naev/ directory')

    args = parser.parse_args()

    basepath = os.path.abspath(args.basepath)
    missionpath = os.path.abspath(basepath + '/dat/missions')
    insanity = sanitizer(basepath=basepath, missionpath=missionpath,
                         verbose=args.verbose,use=args.use,
                         show_unused=args.show_unused)

    insanity.dah_doctor()