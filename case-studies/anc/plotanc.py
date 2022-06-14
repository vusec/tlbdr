#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import numpy as np
import matplotlib.pyplot as plt

# TWEAKABLES
NRUNS = 10 # Runs per measurement point
AVGN = 11  # Running average window size

# Widths (in pts) for common paper formats
USENIX_TEXTWIDTH = 505.89
USENIX_COLUMNWIDTH = 241.02039
IEEE_TEXTWIDTH = 516
IEEE_COLUMNWIDTH = 252

DEFAULT_WIDTH = USENIX_COLUMNWIDTH
DEFAULT_HMULT = 0.8

LABELS = ['Original', 'Optimized']

# END TWEAKABLES

def set_size(width_pt, fraction=1, subplots=(1, 1)):
	"""Set figure dimensions to sit nicely in our document.

	Parameters
	----------
	width_pt: float
			Document width in points
	fraction: float, optional
			Fraction of the width which you wish the figure to occupy
	subplots: array-like, optional
			The number of rows and columns of subplots.
	Returns
	-------
	fig_dim: tuple
			Dimensions of figure in inches
	"""
	# Width of figure (in pts)
	fig_width_pt = width_pt * fraction
	# Convert from pt to inches
	inches_per_pt = 1 / 72.27

	# Golden ratio to set aesthetic figure height
	golden_ratio = (5**.5 - 1) / 2

	# Figure width in inches
	fig_width_in = fig_width_pt * inches_per_pt
	# Figure height in inches
	fig_height_in = fig_width_in * golden_ratio * (subplots[0] / subplots[1])

	return (fig_width_in, fig_height_in)


def fig_size(width_pt, hmult=1):
	w, h = set_size(width_pt)
	return (w, h * hmult)


def doplot(nruns=NRUNS, avgn=AVGN, w=DEFAULT_WIDTH, hmult=DEFAULT_HMULT, ofname='anc-comp.pdf', fmt='pdf'):
	nts = np.fromiter(open(f'anc-naive-{nruns}.txt'), float)
	jts = np.fromiter(open(f'anc-ninja-{nruns}.txt'), float)
	nts /= nruns
	jts /= nruns

	CVV = np.ones((avgn,))/avgn

	avn = np.convolve(nts, CVV, mode='valid')
	avj = np.convolve(jts, CVV, mode='valid')
	avg_n = np.average(nts)
	avg_j = np.average(jts)

	ddiff = (len(nts) - len(avn)) // 2
	assert(ddiff == len(nts) - len(avn) - ddiff)
	avx = np.fromiter(range(ddiff, len(nts)-ddiff), int)

	sz = fig_size(w, hmult)
	print(sz)
	fig, ax = plt.subplots(1, 1, figsize=sz)

	ax.plot(nts, 'C0.:', label=LABELS[0], linestyle='')
	ax.plot(avx, avn, 'C0-')
	ax.plot(jts, 'C1.:', label=LABELS[1], linestyle='')
	ax.plot(avx, avj, 'C1-')
	ax.set_xlabel('Run')
	ax.set_ylabel('Time [s] to break ASLR')
	ax.legend(loc='best')


	fig.tight_layout()
	fig.savefig(ofname, format=fmt)
	plt.close(fig)

	print(avg_j/avg_n)


if __name__ == '__main__':
	doplot()
