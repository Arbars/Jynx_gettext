
#pragma once

#include <vector>
#include <string>
#include <stdexcept>



class CommandLineParsingException: public std::runtime_error
{
public:
    CommandLineParsingException( const char *message ) : std::runtime_error(message) {}
};



void RaiseCommandLineSyntaxErrorException();     // raises CommandLineParsingException
void RaiseCommandLineMissingOperandException();  // raises CommandLineParsingException



template<typename CHAR_TYPE>
std::vector<std::basic_string<CHAR_TYPE>>  MakeParamsListFromArgcAndArgv( int argc, CHAR_TYPE **argv, int startIndex )
{
    std::vector<std::basic_string<CHAR_TYPE>>  paramsList;

    for( int i = startIndex; i < argc; i++)
    {
        paramsList.push_back( argv[i] );
    }

    return paramsList;
}




template<typename STRING_CLASS>
bool CompareWithAscii( const STRING_CLASS &candidateString, const char *asciiString )
{
	STRING_CLASS  str;
	while( *asciiString )
	{
		str += *asciiString;
		++asciiString;
	}
	return str == candidateString;
}




template<typename STRING_CLASS>
bool ParseParamAndValue( const std::vector<STRING_CLASS> &paramList, size_t &i, const char *switchString, STRING_CLASS *out_variable )
{
	if( i < paramList.size() )
	{
		if( CompareWithAscii( paramList[i], switchString ) )
		{
			++i;
			if( i >= paramList.size() )  RaiseCommandLineMissingOperandException();
			if( paramList[i][0] == '-' ) RaiseCommandLineMissingOperandException();
			*out_variable = paramList[i];
			++i;
			return true;
		}
	}
	return false;
}



template<typename STRING_CLASS>
bool ParseParam( const std::vector<STRING_CLASS> &paramList, size_t &i, const char *switchString, bool *out_variable )
{
	if( i < paramList.size() )
	{
		if( CompareWithAscii( paramList[i], switchString ) )
		{
			++i;
			*out_variable = true;
			return true;
		}
	}
	return false;
}


