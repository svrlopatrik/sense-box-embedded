int isBigEndian() {
    int test = 1;
    char *p = (char*)&test;

    return p[0] == 0;
}

void reverseEndianness(const long long int size, void* value){
    int i;
    char result[32];
    for( i=0; i<size; i+=1 ){
        result[i] = ((char*)value)[size-i-1];
    }
    for( i=0; i<size; i+=1 ){
        ((char*)value)[i] = result[i];
    }
}

void toBigEndian(const long long int size, void* value){
    if( ! isBigEndian() ){
        reverseEndianness(size,value);
    }
}
void toLittleEndian(const long long int size, void* value){
    if( isBigEndian() ){
        reverseEndianness(size,value);
    }
}

char bigEndianChar(char value){ char copy = value; toBigEndian(sizeof(char),(void*)&copy); return copy; }
char litEndianChar(char value){ char copy = value; toLittleEndian(sizeof(char),(void*)&copy); return copy; }

int bigEndianInt(int value){ int copy = value; toBigEndian(sizeof(int),(void*)&copy); return copy; }
int litEndianInt(int value){ int copy = value; toLittleEndian(sizeof(int),(void*)&copy); return copy; }

short int bigEndianShort(short int value){ short int copy = value; toBigEndian(sizeof(short int),(void*)&copy); return copy; }
short int litEndianShort(short int value){ short int copy = value; toLittleEndian(sizeof(short int),(void*)&copy); return copy; }

long int bigEndianLong(long int value){ long int copy = value; toBigEndian(sizeof(long int),(void*)&copy); return copy; }
long int litEndianLong(long int value){ long int copy = value; toLittleEndian(sizeof(long int),(void*)&copy); return copy; }

float bigEndianFloat(float value){ float copy = value; toBigEndian(sizeof(float),(void*)&copy); return copy; }
float litEndianFloat(float value){ float copy = value; toLittleEndian(sizeof(float),(void*)&copy); return copy; }

double bigEndianDouble(double value){ double copy = value; toBigEndian(sizeof(double),(void*)&copy); return copy; }
double litEndianDouble(double value){ double copy = value; toLittleEndian(sizeof(double),(void*)&copy); return copy; }