all: run

test: anc-naive-10.txt anc-ninja-10.txt
plot: anc-comp.pdf
run: plot

anc-naive-10.txt anc-ninja-10.txt &: measure.sh
	./measure.sh 20 10

anc-comp.pdf: plot.py plotanc.py anc-naive-10.txt anc-ninja-10.txt
	./plot.py

.PHONY: clean

clean:
	rm -f anc-comp.pdf
	rm -rf revanc __pycache__

cleanall: clean
	rm -f anc-naive-*.txt anc-ninja-*.txt
