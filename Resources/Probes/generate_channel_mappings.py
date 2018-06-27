"""Generates ADchan mappings that describes how to rearrange AD chans such that they are
displayed in vertical spatial order of their corresponding probe channels, taking both
the probe layout and the adapter mapping into account"""

from spyke import probes
import numpy as np

probenames =   ['A1x32',                 'A1x64'                     ]
adapternames = ['Adpt_A32_OM32_RHD2132', 'Adpt_A64_OM32x2_sm_RHD2164']

for probename, adaptername in zip(probenames, adapternames):

    # instantiate probe and adapter objects:
    probe = probes.getprobe(probename)
    adapter = probes.getadapter(adaptername)

    # get probe chans sorted by spatial order:
    spatiallysortedprobechans = probe.chans_lrtb # left to right, top to bottom

    # get AD chans in spatiallysortedprobechan order:
    mapping = [ adapter.probe2AD[probechan] for probechan in spatiallysortedprobechans ]

    print(probename + '__' + adaptername)
    print(mapping)
