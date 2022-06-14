#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import numpy as np

import matplotlib
# matplotlib.use('pgf')
import matplotlib.pyplot as plt

plt.style.use('seaborn-paper')
plt.rcParams.update({
	"font.family": "serif",  # use serif/main font for text elements
	"text.usetex": True,     # use inline math for ticks
	"pgf.rcfonts": False     # don't setup fonts from rc parameters
})

import plotanc


if __name__ == '__main__':
	# plotanc.doplot(ofname='anc-comp.pgf', fmt='pgf')
	plotanc.doplot(ofname='anc-comp.pdf', fmt='pdf')
