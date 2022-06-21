#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import glob

import numpy as np
import matplotlib.pyplot as plt

# TWEAKABLES
RESDIR = 'results'

HISTGLOB = f'{RESDIR}/r*.tct'
COMPFN = f'{RESDIR}/r4.3.tct'
COMPRUN = 0

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


# Get data
l2arr = lambda l: np.fromiter(l.split(), int)


def getds(fname):
	with open(fname) as f:
		ln = list(f)
		return [(x.strip(), l2arr(ln[i+1]), l2arr(ln[i+2])) for i,x in enumerate(ln) if x.endswith('timing run\n')]

def globds(fglob):
	for fn in glob.glob(fglob):
		yield getds(fn)

PCHAM = lambda l: 'PC ' in l
PCTHAM = lambda l: 'PCT' in l

TCHAM = lambda l: '(T+C)' in l
CTHAM = lambda l: '(C+T)' in l

def fold(l, step):
	return [l[i:i+step] for i in range(0, len(l), step)]

def ds2xdx(ds, cond=PCHAM):
	dx = [x for x in ds if cond(x[0])]
	for i,x in enumerate(dx):
		if i and x[0] == dx[0][0]:
			per = i
			break
	else:
		per = len(dx)
	return fold(dx, per) if per else []


def dblxdx(ds, prcond=lambda l: True):
	return (ds2xdx(ds, lambda l: PCHAM(l) and prcond(l)), ds2xdx(ds, lambda l: PCTHAM(l) and prcond(l)))

def glob2pcts(fnglob, cond=lambda l: PCTHAM(l) and TCHAM(l)):
	nvs = []
	jvs = []
	for ds in globds(fnglob):
		xdxt = ds2xdx(ds, cond)
		nvs.extend(d[0][2] for d in xdxt)
		jvs.extend(d[1][2] for d in xdxt)
	return np.concatenate(nvs), np.concatenate(jvs)


# Plot
def orient2utils(vert, ds, ax):
	if vert:
		return ds, LABELS, ax.set_ylabel
	else:
		return reversed(ds), reversed(LABELS), ax.set_xlabel

def sz2fig(figwidth, hmult):
	sz = fig_size(figwidth, hmult)
	print(sz)
	return plt.subplots(1, 1, figsize=sz)


def pgplt(ds, ofname, bs=8192, start=0, *, figwidth=DEFAULT_WIDTH, hmult=DEFAULT_HMULT, fmt='_:', vert=True, ofmt='pdf'):
	fig, ax = sz2fig(figwidth, hmult)

	dit, lbl, labelf = orient2utils(vert, ds, ax)
	boxd = [d[2][start:start+bs] for d in dis]

	# ax.boxplot(boxd, sym='.', labels=['Vanilla', 'Superoptimal'], vert=vert)
	ax.boxplot(boxd, sym='.', labels=lbl, vert=vert)
	labelf('CPU cycles per hammer attempt')
	# for d in ds:
		# ax.plot(d[2][start:start+bs], fmt, label=d[0], **kwargs)
	# ax.legend()

	fig.tight_layout()
	fig.savefig(ofname, format=ofmt)
	plt.close(fig)

	return np.median(ds[1][2])/np.median(ds[0][2])

def phgplt(ds, ofname, bs=8192, start=0, bins=50, *, figwidth=DEFAULT_WIDTH, hmult=DEFAULT_HMULT, ofmt='pdf'):
	fig, ax = sz2fig(figwidth, hmult)

	histv = [d[2][start:start+bs] for d in ds]

	ax.hist(histv, bins=bins, label=LABELS, alpha=0.65)
	ax.set_xlabel('Cycles per hammer attempt')
	ax.set_yticks([])
	ax.legend(loc='center right')

	ax2 = ax.twinx()
	ax2.boxplot(list(reversed(histv)), sym='', labels=list(reversed(LABELS)), vert=False, medianprops={'color':'k'})

	fig.tight_layout()
	fig.savefig(ofname, format=ofmt)
	plt.close(fig)

	return np.median(ds[1][2])/np.median(ds[0][2])


def bpaplt(fnglob, ofname, *, figwidth=DEFAULT_WIDTH, hmult=DEFAULT_HMULT, vert=True, ofmt='pdf'):
	fig, ax = sz2fig(figwidth, hmult)

	dit, lbl, labelf = orient2utils(vert, glob2pcts(fnglob), ax)
	boxd = list(dit)

	ax.boxplot(boxd, sym='', labels=lbl, vert=vert)
	labelf('CPU cycles per hammer attempt')

	fig.tight_layout()
	fig.savefig(ofname, format=ofmt)
	plt.close(fig)


def hgplt(fnglob, ofname, bins=50, *, figwidth=DEFAULT_WIDTH, hmult=DEFAULT_HMULT, ofmt='pdf'):
	fig, ax = sz2fig(figwidth, hmult)

	histv = glob2pcts(fnglob)
	hrange = (min(histv[0].min(), histv[1].min()), 9500)

	ax.hist(histv, bins=bins, range=hrange, label=LABELS, alpha=0.65)
	ax.set_xlabel('Cycles per hammer attempt')
	ax.set_yticks([])
	ax.legend(loc='center right')

	ax2 = ax.twinx()
	ax2.boxplot(list(reversed(histv)), sym='', labels=list(reversed(LABELS)), vert=False, medianprops={'color':'k'})

	fig.tight_layout()
	fig.savefig(ofname, format=ofmt)
	plt.close(fig)


def doplot(ofmt='pdf'):
	# Plot hist
	hgplt(HISTGLOB, f'./pthammer-hist.{ofmt}', ofmt=ofmt)
	# Plot comp
	xdxt = ds2xdx(getds(COMPFN), lambda l: TCHAM(l) and PCTHAM(l))
	print(phgplt(xdxt[COMPRUN], f'./pthammer-comp.{ofmt}', ofmt=ofmt))


if __name__ == '__main__':
	doplot()
