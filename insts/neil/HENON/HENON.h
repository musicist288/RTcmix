#include <Instrument.h>

class HENON : public Instrument {

public:
	HENON();
	virtual ~HENON();
	virtual int init(double *, int);
	virtual int configure();
	virtual int run();

private:
	void doupdate();

	int nargs, branch;
	float amp, pan, a, b, x, y, z;
};

