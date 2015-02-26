// COM_control.cpp : Defines the entry point for the console application.
//
//NOTE: Program needs administrative rights. Done by editing .exe to require
//			admin

#include "stdafx.h"
#include "ziAPI.h"
#include <conio.h>
#include <stdio.h>
#include <string>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <WinSock2.h>
#include <fstream>
#include "pid.h"

using namespace std;
#define COM1       0
#define DATA_READY 0x100
#define SETTINGS ( 0x80 | 0x02 | 0x00 | 0x00)

#pragma comment(lib,"ziAPI.lib") 
#pragma comment(lib,"ws2_32.lib")


//time between steps
	//
//voltage (size) stepsize
	//100um / 150V @300K

HANDLE hComJ;
HANDLE hComA;
//com port handles

// PID Controller



double jenaX;
double jenaY;
double jenaZ;
double oldjenaZ;
//current value of each axis

bool first;
//small bug fix

double attoZ;
//atto location

//special parameters for debug mode
bool debug;
double delay;

//parameters for op mode
bool xon;
bool yon;
bool zon;
double xdiff;
double ydiff;
double zdiff;

int zsteps=0;

//Track events for averaging
int event_count=0;
double event_sum=0.0;

static double nmToVolt = 150.0 / 150000;

ziConnection Conn;
ofstream myfile;
ofstream PIDfile;
ifstream inputfile;


BOOL WriteABuffer(HANDLE* hCom, char* lpBuf, DWORD dwToWrite)
{
   OVERLAPPED osWrite = {0};
   DWORD dwWritten;
   BOOL fRes;

   // Create this writes OVERLAPPED structure hEvent.
   osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
   if (osWrite.hEvent == NULL)
      // Error creating overlapped event handle.
      return FALSE;
   char* send = new char[dwToWrite];
   send = lpBuf;
   // Issue write.
   if (!WriteFile(*hCom, send, dwToWrite, &dwWritten, &osWrite)) {
      if (GetLastError() != ERROR_IO_PENDING) { 
		  cout << "ERROR writing: " << GetLastError() << endl;
         // WriteFile failed, but it isn't delayed. Report error and abort.
         fRes = FALSE;
		 
      }
      else {
         // Write is pending.
		  cout << "ERROR writing: " << GetLastError() << endl;
         if (!GetOverlappedResult(*hCom, &osWrite, &dwWritten, TRUE))
            fRes = FALSE;
         else
            // Write operation completed successfully.
            fRes = TRUE;
      }
   }
   else
      // WriteFile completed immediately.
      fRes = TRUE;

   CloseHandle(osWrite.hEvent);
   //delete [] send;
   return fRes;
}

BOOL ReadABuffer(HANDLE* hCom, char* lpBuf, DWORD dwToWrite)
{
   OVERLAPPED osWrite = {0};
   DWORD dwWritten;
   BOOL fRes;

   // Create this writes OVERLAPPED structure hEvent.
   osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
   if (osWrite.hEvent == NULL)
      // Error creating overlapped event handle.
      return FALSE;
   char* send = new char[dwToWrite];
   send = lpBuf;
   // Issue write.
   if (!ReadFile(*hCom, lpBuf, dwToWrite, &dwWritten, &osWrite)) {
      if (GetLastError() != ERROR_IO_PENDING) { 
		  cout << "ERROR reading: " << GetLastError() << endl;
         // WriteFile failed, but it isn't delayed. Report error and abort.
         fRes = FALSE;
		 
      }
      else {
         // Write is pending.
		  cout << "ERROR reading: " << GetLastError() << endl;
         if (!GetOverlappedResult(*hCom, &osWrite, &dwWritten, TRUE))
            fRes = FALSE;
         else
            // Write operation completed successfully.
            fRes = TRUE;
      }
   }
   else
      // WriteFile completed immediately.
      fRes = TRUE;

   CloseHandle(osWrite.hEvent);
   //delete [] send;
   return fRes;
}

void checkXYZ(double &x, double &y, double &z)
{
	char* send = new char[6];
	char* read = new char[15];
	
	sprintf_s(send, 23, "rk,0\r");
	WriteABuffer(&hComJ, send, 5);
	Sleep(3);
	
	ReadABuffer(&hComJ, read, 14);
	sscanf(read+5,"%lf",&x);
	
	fill(read, read+10, 0);
	send[3] = '1';
	WriteABuffer(&hComJ, send, 5);
	Sleep(3);
	ReadABuffer(&hComJ, read, 14);
	sscanf(read+5,"%lf",&y);

	fill(read, read+10, 0);
	send[3] = '2';
	WriteABuffer(&hComJ, send, 5);
	Sleep(3);
	ReadABuffer(&hComJ, read, 14);
	sscanf(read+5,"%lf",&z);

	cout<< "Current Values\nX: " << x <<"\nY: "<< y << "\nZ: "<<z<<endl;

}


void DoneWithAtto();
void stepAtto()
{
	double um=-1;
	fprintf(stdout, "How many um would you like to drop?\n1-1000:");
	scanf_s("%d",&um);
	if(um == -1) return;
	//for(int i=0;i<um;i++) step size is 15V -> 0.45um/step
	char* send = new char[20];
	sprintf_s(send, 20, "stepd 2 %d\r\n", ((int) (um/0.45))); //move 500nm
		WriteABuffer(&hComA, send, 20); 
	fprintf(stdout, "Dropped %d um.\n",um);
	attoZ+=((int) um/0.45)*0.45;
	DoneWithAtto();
}

void setJena(char loc, double val)
{
	int xyz;
	if(loc > 'a') xyz = loc-'x';
	else xyz = loc-'X';
	char* send = new char[23];
	sprintf_s(send,23,"set,%d,%lf\r", xyz, val);
	int ct = 0;
	while(send[ct]!='\r') ct++;
	WriteABuffer(&hComJ, send, ct+1);
	delete [] send;
}

void DoneWithAtto()
{
	char c;
	fprintf(stdout, "Drop more?\ny or n:");
	c=getchar();
	if(!first) c=getchar();
	else first = false;
	switch(c)
	{
	case 'y':
		stepAtto();
	case 'n':
		break;
	default:
		fprintf(stdout, "Invalid. Please try again.\n");
		DoneWithAtto();
	}
}
void EventLoop( ziConnection Conn );
void RaiseJenaZ()
{
	fprintf(stdout, "How many Volts?: ");
	double dv;
	int temp = 0;
	bool useif = inputfile.good();
	char * stepsize = new char[10];
	char c=0;
	while(inputfile.good() && c!= '\n' && temp<10)
	{
		c = inputfile.get();
		stepsize[temp++] = c;
	}
	if(useif)
	{
		sscanf(stepsize,"%lf",&dv);
		cout << dv << endl;
	}
	else
		scanf_s("%lf",&dv);
	delete [] stepsize;

	//fflush(stdin);
	if(dv > 5){
		cerr << "ERROR: Jump value too large!!! exiting\nPress any key";
		getchar();
		exit(1);
	}
	
	
	
	jenaZ+=dv;
	setJena('z',jenaZ);
	//fflush(stdin);
	fprintf(stdout, "Done.\nAgain? y or n:");
	//char waste = getchar();
	c= getchar();
	while(c<64) c = getchar();
	if(c!='n') RaiseJenaZ();
	
}
void RasterJenaZ() {

	double datapoint=0.0;
	char c;
	cout<<"Confirm/repeat Z raster?\n";
	c=getchar();
	if (c<64) c = getchar();
	bool done = false;
	while(!done){
		if(c=='y') 
	{ 


		int numChars = 17;
		bool subYet = false;
		fprintf(stdout, "Resetting Jena Z value\n");
		jenaZ = oldjenaZ;
		setJena('z',jenaZ);
		Sleep(3);
		
		
		cout<<"Measuring baseline before Z raster.  \n";
		myfile<<"Baseline before Z raster \n";
		for(int count=0;count<3;count++)
		{
			
			EventLoop(Conn);
			datapoint=(event_sum/event_count);
			myfile<<fixed<<setprecision(9)<<(jenaZ+(count*zdiff)-(25*zdiff)) <<"\t"<<datapoint<<"\n";
			cout<<fixed<<setprecision(9)<<jenaZ<<"\t"<<datapoint<<"\n";
		}
		myfile<<"\n\n\n Start of Z raster data:\n";

		fprintf(stdout, "Beginning Raster: jena Z\n");
		for(int x = 0; x<zsteps; x++) //each step for movement
		{
			myfile<<fixed<<setprecision(4)<<jenaZ<<"\t";
			setJena('z',jenaZ);
			jenaZ+=zdiff;
			//Sleep(4000);
		
			//DO DATA AQUISITION AND OTHER THINGS HERE
			EventLoop(Conn);
			datapoint=(event_sum/event_count);
			myfile<<fixed<<setprecision(9)<<datapoint<<"\n";
			cout<<fixed<<setprecision(3)<<jenaZ-zdiff<<"  "<<setprecision(9)<<datapoint<<"\n";

		}
		cout<<"Raster Completed.\n";
		cout<<"Repeat Z raster?\n";
		char c = getchar();
	while(c<11) c = getchar();
	if(c == 'y' || c == 'Y')
	{
		fprintf(stdout, "Step Voltage in Raster (V): ");
		double dz;
		scanf_s("%lf", &dz);
		zdiff = dz;
		fflush(stdin);
		if(zdiff > 1)
		{
			cout<< "ERROR: step voltage too large!!! exiting\nPress any key";
			getchar();
			exit(1);
		}
		fprintf(stdout, "Number of steps: ");
		int z;
		scanf_s("%d", &z);
		zsteps = z;
		fflush(stdin);
		oldjenaZ=jenaZ;
		///!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//FIX THIS VOLTAGE >>><<< below!
		if(zsteps * zdiff > 20)
		{
			cout << "ERROR: range of operation too large!!! exiting\nPress any key";
			getchar();
			exit(1);
		}
		
	}
	else if(c=='n') done = true;
	else {
			cout <<"Try again: y or n: ";
			c=getchar();
			if (c<64) c = getchar();
		}
	}
}
}

//should I reset the value after the jenaZ raster or leave it be???

void RaiseJena()
{
	fprintf(stdout, "Would you like to RASTER the Jena Z?\ny or n: ");
	char c;
	if(inputfile.good())
	{
		c = inputfile.get();
		cout << c << endl;
		inputfile.get();
	}
	else
	{
		c = getchar();
		while(c<11) c = getchar();
	}
	if(c == 'y' || c == 'Y')
	{
		fprintf(stdout, "Step Voltage in Raster (V): ");
		double dz;
		char* stepsize = new char[10];
		int temp = 0;
		bool useif = inputfile.good();
		while(inputfile.good() && c!= '\n' && temp<10)
		{
			c = inputfile.get();
			stepsize[temp++] = c;
		}
		if(useif)
		{
			sscanf(stepsize,"%lf",&dz);
			cout << dz << endl;
		}
		else
			scanf_s("%lf", &dz);
		zdiff = dz;
		fflush(stdin);
		if(zdiff > 1)
		{
			cout<< "ERROR: step voltage too large!!! exiting\nPress any key";
			getchar();
			exit(1);
		}
		fill(stepsize,stepsize+9,0);
		fprintf(stdout, "Number of steps: ");
		int z;
		temp = 0;
		c=0;
		useif = inputfile.good();
		while(inputfile.good() && c!= '\n' && temp<10)
		{
			c = inputfile.get();
			stepsize[temp++] = c;
		}
		if(useif)
		{
			sscanf(stepsize,"%d",&z);
			cout << z << endl;
		}
		else
			scanf_s("%d", &z);
		zsteps = z;
		fflush(stdin);
		///!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		//FIX THIS VOLTAGE >>><<< below!
		if(zsteps * zdiff > 150)
		{
			cout << "ERROR: range of operation too large!!! exiting\nPress any key";
			getchar();
			exit(1);
		}
		
		RasterJenaZ();
	}
	fprintf(stdout,"Would you like to raise the Jena?\ny or n: ");
	if(inputfile.good())
	{
		c = inputfile.get();
		cout << c << endl;
		inputfile.get();
	}
	else
	{
		c = getchar();
		while(c<11) c = getchar();
	}
	switch(c) {
	case 'y':
		RaiseJenaZ();
	case 'n':
		break;
	default:
		fprintf(stdout, "Invalid, try again.\n");
		RaiseJena();
	}
}

void EventLoop( ziConnection Conn );

void RasterJena()
{
	fprintf(stdout, "Resetting Jena XY values\n");
	setJena('x', jenaX);
	Sleep(3);
	setJena('y', jenaY);
	//test if file is open;
	cout<<myfile.is_open()<< "  File is open\n";
	PIDfile.open ("PID settling data.txt",ios::out);
	cout<<PIDfile.is_open()<<"  PID file is open\n";
	myfile << fixed << setprecision(6) << "Starting values Jena X: "<<jenaX <<"\tJenaY: "<<jenaY<<endl;

	double oldX = jenaX;
	double oldY = jenaY;
	//myfile.close();

	int xs = 0;
	int ys = 0;

	printf("Number of raster steps in x direction: ");
	int temp = 0;
	bool useif = inputfile.good();
	char* stepsize = new char[10];
	char c = 0;
	while(inputfile.good() && c!= '\n' && temp<10)
	{
		c = inputfile.get();
		stepsize[temp++] = c;
	}
	if(useif)
	{
		sscanf(stepsize,"%d",&xs);
		cout << xs << endl;
	}
	else
		scanf_s("%d",&xs);
	printf("Number of raster steps in y direction: ");
	temp = 0;
	useif = inputfile.good();
	fill(stepsize, stepsize+9, 0);
	c=0;
	while(inputfile.good() && c!= '\n' && temp<10)
	{
		c = inputfile.get();
		stepsize[temp++] = c;
	}
	if(useif)
	{
		sscanf(stepsize,"%d",&ys);
		cout << ys << endl;
	}
	else
		scanf_s("%d", &ys);

	fprintf(stdout, "Beginning Raster...\n");
	for(int x = 0; x<xs; x++) //each step is 666 nm/V
	{
		
		//myfile.open(filename,std::ofstream::app);
		myfile<<"\n"<<x<<"\t";
		setJena('x',jenaX);
		jenaX+=xdiff;
		setJena('y',jenaY);
		
		
		
		
		if(!yon){
			//Sleep(10);
			EventLoop(Conn);
			double datapoint = (event_sum/event_count);
			myfile << fixed << setprecision(6) << datapoint << "\t"; //tab cha
		}
		else
		
		for(int y = 0; y<ys; y++)
		{
			double datapoint=0;
			setJena('y', jenaY);
			jenaY+=ydiff;
			
			if(debug && (delay != 0)) Sleep(delay);
			//else Sleep(3);



			//DO DATA AQUISITION AND OTHER THINGS HERE
			int pid=0,lock_sum=0,lock_cnt=0;
			int pid_lock[10];
			for(lock_cnt=0;lock_cnt<5;lock_cnt++)
				pid_lock[lock_cnt]=200;
		struct _pid PID;
		double process_variable=38, set_point = 4200, data_point = 0.0;
		pid_init(&PID, &process_variable, &set_point);
		pid_tune(&PID, .8, 0.0, .2,300);
		cout << fixed << setprecision(6) << process_variable << "\n";
		for(;;) {
			lock_sum=0;
			
			for(lock_cnt=0;lock_cnt<5;lock_cnt++)			lock_sum+=pid_lock[lock_cnt];
			if(lock_sum==0) break;
			 //get in freq
			Sleep(4000);
			EventLoop(Conn);
			data_point = (event_sum/event_count); 
			
			//get int 
			int intFactor = 1000;
			process_variable = (int) (intFactor * data_point);
			//double i = process_variable*intFactor;
			//get V
			double VperHz = (.05)/(10000.0);
			pid =  (int) pid_calc(&PID);
			double voltage = pid * VperHz;
			//double voltage = i*voltageFactor;
			for(lock_cnt=5;lock_cnt>0;lock_cnt--)
				pid_lock[lock_cnt]=pid_lock[lock_cnt-1];
			pid_lock[0]=pid;
			//show all

			
			cout << "freq in: " << data_point << "\tPID val: " << pid << "\tVout: " << voltage<<"\tjenaZ:  "<<jenaZ+voltage << endl;
			cout<<"\t\t "<< y <<"  jenaY:"<<jenaY<<"\t "<< x <<" jenaX: "<<jenaX<<"\t"<<event_count<<endl;
			PIDfile << "freq in: " << data_point << "\tPID val: " << pid << "\nVout: " << voltage<<"\tjenaZ:  "<<jenaZ+voltage<<"\t jenaY:"<<jenaY<<"\t jenaX: "<<jenaX<<endl;
			if(pid != 0 && abs(pid) < 30000 && process_variable < 30000 )
			{	
				jenaZ+=voltage;
				char* send = new char[23];
				setJena('z',jenaZ);
				delete[] send;
				
				
			}	
			
		}
		myfile<<fixed<<setprecision(9)<<"\t"<<jenaZ;
		//Sleep(1000);
		//return 0; //false end
			//EventLoop(Conn);
			//datapoint=(event_sum/event_count);
			//myfile<<fixed<<setprecision(9)<<datapoint<<"\t";
			
			

		}
		ydiff*=-1;
		
	}


	fprintf(stdout, "Raster Completed.\n");
}
void DropAtto()
{
	//drop atto 3mm
	attoZ=0;
	fprintf(stdout,"Dropping AttoCube 4mm...\n");
	WriteABuffer(&hComA, "ver\r\n",5);
	WriteABuffer(&hComA, "setm 2 stp\r\n",12); //set mode to stp - step
	WriteABuffer(&hComA, "setf 2 1000\r\n",13); //set frequency
	WriteABuffer(&hComA, "setv 2 5.0\r\n",12); //step voltage 15
	WriteABuffer(&hComA, "setf 2 1000\r\n",13);
	//temporarily do not drop 3mm
	//WriteABuffer(&hComA, "stepu 2 5000\r\n", 15);
	//WriteABuffer(&hComA, "stepd 2 4445\r\n",14); //step 100 steps
	fprintf(stdout,"Done.\n");
	//attoZ += 4445*0.45;
}

//****IMPORTANT!--REMOVED ATTO FUNCTIONS TO TEST FOR PERFORMANCE*****
bool Perform_Raster()
{
	//ATTO functions
	
	//DropAtto();
	//DoneWithAtto();

	//JENA functions
	
	
	//char c=getchar();
	RaiseJena();

	char c=0;
	cout<<"Would you like to raster XY? \ny or n: ";
	if(inputfile.good())
	{
		c = inputfile.get();
		cout << c << endl;
		inputfile.get();
	}
	else
		while(c<64) c = getchar();
	bool done = false;
	while(!done){
		if(c=='y') {RasterJena(); done = true;}
		else if(c=='n') done = true;
		else {
			cout <<"Try again: y or n: ";
			c=getchar();
			while (c<64) c = getchar();
		}
	}
	return true;
}

//setup functions for raster
void setupOp()
{
	xdiff = 0;
	ydiff = 0;
	char c;
	char d;
	double dv = 0;
	fprintf(stdout, "Do you want x axis? (y or n): ");
	if(inputfile.good())
	{
		c = inputfile.get();
		cout << c << endl;
		inputfile.get();
	}
	else
	{
		c=getchar();
		fflush(stdin);
	}
	bool largeinput = false;
	int temp;
	bool useif;
	char* stepsize = new char[10];
	switch(c)
	{
	case 'y':
		xon = true;
		fprintf(stdout, "What step size for x? (V): ");
		
		temp = 0;
		useif = inputfile.good();
		while(inputfile.good() && c!= '\n' && temp<10)
		{
			c = inputfile.get();
			stepsize[temp++] = c;
		}
		if(useif)
		{
			sscanf(stepsize,"%lf",&dv);
			cout << dv << endl;
		}
		else
		{
			scanf_s("%lf", &dv);
			fflush(stdin);
		}
		xdiff = dv;
		if(xdiff > 3) largeinput = true;
		break;
	case 'n':
		xon = false;
		xdiff = 0;
		break;
	default:
		fprintf(stdout, "Invalid. Please try again.\n");
		setupOp();
	}

	fprintf(stdout, "Do you want y axis? (y or n): ");
	if(inputfile.good())
	{
		d = inputfile.get();
		cout << d << endl;
		inputfile.get();
	}
	else
	{
		d=getchar();
	}
	switch(d)
	{
	case 'y':
		yon = true;
		fprintf(stdout, "What step size for y? (V): ");
		temp = 0;
		useif = inputfile.good();
		while(inputfile.good() && c!= '\n' && temp<10)
		{
			c = inputfile.get();
			stepsize[temp++] = c;
		}
		if(useif)
		{
			sscanf(stepsize,"%d",&dv);
			cout << dv << endl;
		}
		else
		{
			scanf_s("%lf", &dv);
			fflush(stdin);
		}
		ydiff = dv;
		if(ydiff > 3) largeinput = true;
		break;
	case 'n':
		yon = false;
		ydiff = 0;
		break;
	default:
		fprintf(stdout, "Invalid. Please try again.\n");
		setupOp();
	}
	delete [] stepsize;
	if(largeinput)
	{
		cout << "ERROR: step voltage way too large!!! exiting\nPress any key";
		getchar();
		exit(1); 
	}
}
void setupDebug()
{
	fprintf(stdout, "Time between each step (ms): ");
	double ms;
	scanf_s("%d", &ms);
	fflush(stdin);

	delay = ms;
}

bool OpMode_DebugMode()
{
	debug = false;
	delay = 0;
	fprintf(stdout, "Op mode (1) or Debug mode (2)? (1 or 2): ");
	char c;
	if(inputfile.good())
	{
		c = inputfile.get();
		cout << c << endl;
		inputfile.get(); //newline
	}
	else
	{
		c = getchar();
		fflush(stdin);
	}
	switch(c)
	{
	case '2':
		setupDebug();
	case '1':
		setupOp();
		Perform_Raster();
		break;
	default:
		fprintf(stdout, "Invalid, try again.\n");
		OpMode_DebugMode();
	}
	return true;
}

bool ConnectAndSetup()
{

	//add rk requests to Jena to get current voltages for all three channels and set to them.
	
	
	zdiff = 0.0;
	zsteps = 0;
	first = true;

	
	 COMMTIMEOUTS  lpToJ;
	 COMMTIMEOUTS  lpToA;
	 COMMCONFIG  lpCCJ;
	 COMMCONFIG  lpCCA;
	// DWORD dwWrittenJ;
	// DWORD dwWrittenA;
	 char str_comJ[10];
	 char str_comA[10];

	 unsigned short  no_com_jena = 6;
	 unsigned short no_com_atto = 4;
	 //Designates COM port for jena and atto


	 lpCCJ.dcb.DCBlength = sizeof(DCB);
	 lpCCA.dcb.DCBlength = sizeof(DCB); 

	 //give proper format for COM port request in str_com
	 sprintf_s( str_comJ, "\\\\.\\COM%d\0", no_com_jena);
	 sprintf_s( str_comA, "\\\\.\\COM%d\0", no_com_atto);

	 
	 //TO WORK, MUST RUN AS ADMINISTRATOR. CHANGE PERMISSIONS OF COM_CONTROL.EXE
	 hComJ = CreateFileA(str_comJ,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
	
	 if (hComJ == INVALID_HANDLE_VALUE)
		printf("Error number: %ld\n", GetLastError());
	 else
        printf("success with piezojena\n") ;

	 //Must be Admin. Open atto port.
	 hComA = CreateFileA(str_comA,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);

	 if (hComA == INVALID_HANDLE_VALUE)
		printf("Error number: %ld\n", GetLastError());
	 else
        printf("success with attocube\n") ;

	 //ensure proper function, allow set parameters
	 GetCommState(hComJ, &lpCCJ.dcb);
	 GetCommState(hComA,&lpCCA.dcb);

	 //delete wcstrJ;
	// delete wcstrA;

	 /* Parameter declaration */
	 lpCCJ.dcb.BaudRate = CBR_19200;//change from CBR_9600;
	 lpCCJ.dcb.ByteSize = 8;
	 lpCCJ.dcb.StopBits = ONESTOPBIT;
	 lpCCJ.dcb.Parity = NOPARITY;
	 lpCCJ.dcb.fOutX = TRUE;
	 lpCCJ.dcb.fInX = TRUE;
	
	 lpCCA.dcb.BaudRate = CBR_38400;//change from CBR_9600;
	 lpCCA.dcb.ByteSize = 8;
	 lpCCA.dcb.StopBits = ONESTOPBIT;
	 lpCCA.dcb.Parity = NOPARITY;
	 lpCCA.dcb.fOutX = TRUE;
	 lpCCA.dcb.fInX = TRUE;
	// lpCC.dcb.XonChar = 0x11; //should be newline and carriage return
	// lpCC.dcb.XoffChar = 0x13;

	 lpCCJ.dcb.fDtrControl = DTR_CONTROL_DISABLE;
	 lpCCJ.dcb.fRtsControl = RTS_CONTROL_DISABLE;

	 lpCCA.dcb.fDtrControl = DTR_CONTROL_DISABLE;
	 lpCCA.dcb.fRtsControl = RTS_CONTROL_DISABLE;
	
	 if(!(SetCommState( hComJ, &lpCCJ.dcb ))) cout << "Error setting COM Jena: "<<GetLastError()<<endl;
	 if(!SetCommState( hComA, &lpCCA.dcb )) cout << "Error setting COM Atto: "<<GetLastError()<<endl;

	 //set Timeouts.
	 GetCommTimeouts(hComJ,&lpToJ);
	 lpToJ.ReadIntervalTimeout = 0;
	 lpToJ.ReadTotalTimeoutMultiplier = 10;
	 lpToJ.ReadTotalTimeoutConstant = 10;
	 lpToJ.WriteTotalTimeoutMultiplier = 10;
	 lpToJ.WriteTotalTimeoutConstant = 100;
	 SetCommTimeouts(hComJ,&lpToJ);

	 SetupComm(hComJ,2048,2048);

	 GetCommTimeouts(hComA,&lpToA);
	 lpToA.ReadIntervalTimeout = 0;
	 lpToA.ReadTotalTimeoutMultiplier = 10;
	 lpToA.ReadTotalTimeoutConstant = 10;
	 lpToA.WriteTotalTimeoutMultiplier = 10;
	 lpToA.WriteTotalTimeoutConstant = 100;
	 SetCommTimeouts(hComA,&lpToA);

	 SetupComm(hComA,2048,2048);

	WriteABuffer(&hComJ, "setk,0,0\r\n", 11);
	WriteABuffer(&hComJ, "setk,1,0\r\n", 11);
	WriteABuffer(&hComJ, "setk,2,0\r\n", 11);

	checkXYZ(jenaX, jenaY, jenaZ);
	oldjenaZ = jenaZ;
	 return 0;
}

void ProcessEvent( ziEvent* Event )
{
	int j;
	switch( Event->Type )
	{
		case ZI_DATA_DOUBLE:
			//printf( "%u elements of double data %s\n",Event->Count,Event->Path );
			for( j = 0; j < Event->Count; j++ ){
				event_sum+=Event->Val.Double[j];
				event_count++;
			}
			break;
		case ZI_DATA_INTEGER:
			printf( "%u elements of integer data %s\n",Event->Count,Event->Path );
			for( j = 0; j < Event->Count; j++ )
			printf( "%lld\n", Event->Val.Integer[j] );
			break;
		case ZI_DATA_DEMODSAMPLE:
			printf( "%u elements of sample data %s\n",
			Event->Count,
			Event->Path );
			for(j = 0; j < Event->Count; j++)
				printf( "TS=%f, X=%f, Y=%f\n",
				Event->Val.SampleDemod[j].TimeStamp,
				Event->Val.SampleDemod[j].X,
				Event->Val.SampleDemod[j].Y );
			break;
		case ZI_DATA_TREE_CHANGED:
			printf( "%u elements of tree-changed data %s\n",
			Event->Count,
			Event->Path );
			for(j = 0; j < Event->Count; j++) {
				switch (Event->Val.Tree[j].Action) {
				case TREE_ACTION_REMOVE:
				printf( "Tree removed: %s\n",
Event->Val.Tree[j].Name );
break;
case TREE_ACTION_ADD:
printf( "Tree added: %s\n",
Event->Val.Tree[j].Name );
break;

case TREE_ACTION_CHANGE:
printf( "Tree changed: %s\n",
Event->Val.Tree[j].Name );
break;
}
}
break;
}
}


void PrintImage(double *matrix, unsigned int rows, unsigned int cols, unsigned int slices)
{
	
}

void EventLoop( ziConnection Conn )
{
	ZI_STATUS RetVal;
	char* ErrBuffer;
	ziEvent* Event;
	/*
	allocate ziEvent in heap memory instead of
	getting it from stack will secure
	against stack overflows especially in windows
	*/
	if( (Event = (ziEvent *)malloc( sizeof( ziEvent ) )) == NULL )
	{
		fprintf( stderr, "Can't allocate memory\n" );
		return;
	}
	//subscribe to freq delta node
	if( ( RetVal = ziAPISubscribe( Conn, "/DEV331/PLLS/0/FREQDELTA" ) ) != ZI_SUCCESS )
	{
	ziAPIGetError( RetVal, &ErrBuffer, NULL );
	fprintf( stderr, "Can't subscribe: %s\n", ErrBuffer );
	free( Event );
	return;
	}
	//loop 1000 times
	event_sum=0.0;
	event_count=0;
	unsigned int Cnt = 0;
	
	while( Cnt < 400000 )
	{
	//get all demod rates from all devices every 10th cycle
	/*if( ++Cnt % 10 == 0 )
	{
	if( ( RetVal =ziAPIGetValueAsPollData(Conn, "/DEV331/PLLS/0/FREQDELTA" ) ) != ZI_SUCCESS )
	{
		ziAPIGetError( RetVal, &ErrBuffer, NULL );
		fprintf( stderr, "Can't get value as poll data: %s\n",
		ErrBuffer );
		break;
	}
	}*/
	//poll data until no more data is available
	Cnt++;

	while( 1 )
	{
	if( ( RetVal = ziAPIPollData(Conn, Event, 0 ) ) != ZI_SUCCESS )
	{
	ziAPIGetError( RetVal, &ErrBuffer, NULL );
	fprintf( stderr, "Can't poll data: %s\n", ErrBuffer );
	break;
	}
	else
	{
	//The field Count of the Event struct is zero when
	//no data has been polled
	if( Event->Type != ZI_DATA_NONE && Event->Count > 0 )
	{
		ProcessEvent(Event);
	}
	else
	{
	//no more data is available so go on
	
		break;
	}
	}
	}
	}
	if( ziAPIUnSubscribe( Conn, "*" ) != ZI_SUCCESS )
	{
		ziAPIGetError( RetVal, &ErrBuffer, NULL );
		fprintf( stderr, "Can't unsubscribe: %s\n", ErrBuffer );
	}
	free( Event );
}

int main(void) {

	myfile.close();
	if(inputfile.is_open()) inputfile.close();
	//char filename[50];
	int scan_number=0, x=0;
	cout<<"Filename for Scan: log.txt\n";
	//	filename = "log.txt";
	
	myfile.open ("log.txt",fstream::out | fstream::app);
	if(myfile.is_open()) cout<<"output file is now open\n";
	myfile<<"Writing this to a file. scan "<<scan_number<<"\n";
	
	inputfile.open("input.txt", ifstream::in);
	if(myfile.is_open()) cout<<"input file is now open\n";

	ConnectAndSetup();
	

	ZI_STATUS RetVal;
	char* ErrBuffer;
	//initialize the ziConnection
	if( ( RetVal = ziAPIInit( &Conn ) ) != ZI_SUCCESS )
	{
		ziAPIGetError( RetVal, &ErrBuffer, NULL );
		fprintf( stderr, "Can't init Connection: %s\n", ErrBuffer );
		return 1;
	}
	//connect to the ziServer running on localhost
	//using the port 8005 (default)
	if( ( RetVal = ziAPIConnect( Conn,"localhost",
	8005 ) ) != ZI_SUCCESS )
	{
		ziAPIGetError( RetVal, &ErrBuffer, NULL );
		fprintf( stderr, "Can't connect: %s\n", ErrBuffer );
	}
	else
	{
		
		
		OpMode_DebugMode();
		myfile.close();
		
		
		
		//disconnect from the ziServer
		//since ZIAPIDisconnect always returns ZI_SUCCESS
		//no error handling is required
		ziAPIDisconnect(Conn);
	}

	ziAPIDestroy( Conn );
	CloseHandle(hComJ);
	CloseHandle(hComA);
	fprintf(stdout, "Program ended, press any key to continue.");
	getchar();
	return 0;

	 
}


