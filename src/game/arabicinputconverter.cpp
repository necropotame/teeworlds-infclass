#include <base/system.h>
#include "arabicinputconverter.h"

struct ArabicCharacter
{
	char const c[5];	//Normal character
	char const c00[5];	//Version without ligature /!\ it may look similar to 'c', but the UTF8 code is different
	char const c10[5];	//Left ligature
	char const c01[5];	//Right ligature
	char const c11[5];	//Left and right ligature
	bool left;
	bool right;
};

struct ArabicCombinaison
{
	char const c[5];	//Normal character
	char const cNext[5];//Next character
	char const c0[5];	//
	char const c1[5];	//
};

#define ADD_CHAR(c, c00) {c, c00, "", "", "", false, false}
#define ADD_CHARLEFTLIGATURE(c, c00, c10) {c, c00, c10, "", "", true, false}
#define ADD_CHARLEFTRIGHTLIGATURE(c, c00, c01, c11, c10) {c, c00, c10, c01, c11, true, true}

static ArabicCharacter g_aArabicCharacters[] =
{
	ADD_CHARLEFTLIGATURE("ا", "ﺍ", "ﺎ"),
	ADD_CHARLEFTRIGHTLIGATURE("ب", "ﺏ", "ﺑ", "ﺒ", "ﺐ"),
	ADD_CHARLEFTRIGHTLIGATURE("ت", "ﺕ", "ﺗ", "ﺘ", "ﺖ"),
	ADD_CHARLEFTRIGHTLIGATURE("ث", "ﺙ", "ﺛ", "ﺜ", "ﺚ"),
	ADD_CHARLEFTRIGHTLIGATURE("پ", "ﭖ", "ﭘ", "ﭙ", "ﭗ"),
	ADD_CHARLEFTRIGHTLIGATURE("ج", "ﺝ", "ﺟ", "ﺠ", "ﺞ"),
	ADD_CHARLEFTRIGHTLIGATURE("ح", "ﺡ", "ﺣ", "ﺤ", "ﺢ"),
	ADD_CHARLEFTRIGHTLIGATURE("خ", "ﺥ", "ﺧ", "ﺨ", "ﺦ"),
	ADD_CHARLEFTRIGHTLIGATURE("چ", "ﭺ", "ﭼ", "ﭽ", "ﭻ"),
	ADD_CHARLEFTLIGATURE("د", "ﺩ", "ﺪ"),
	ADD_CHARLEFTLIGATURE("ذ", "ﺫ", "ﺬ"),
	ADD_CHARLEFTLIGATURE("ر", "ﺭ", "ﺮ"),
	ADD_CHARLEFTLIGATURE("ز", "ﺯ", "ﺰ"),
	ADD_CHARLEFTLIGATURE("ژ", "ﮊ", "ﮋ"),
	ADD_CHARLEFTRIGHTLIGATURE("س", "ﺱ", "ﺳ", "ﺴ", "ﺲ"),
	ADD_CHARLEFTRIGHTLIGATURE("ش", "ﺵ", "ﺷ", "ﺸ", "ﺶ"),
	ADD_CHARLEFTRIGHTLIGATURE("ص", "ﺹ", "ﺻ", "ﺼ", "ﺺ"),
	ADD_CHARLEFTRIGHTLIGATURE("ض", "ﺽ", "ﺿ", "ﻀ", "ﺾ"),
	ADD_CHARLEFTRIGHTLIGATURE("ط", "ﻁ", "ﻃ", "ﻂ", "ﻂ"),
	ADD_CHARLEFTRIGHTLIGATURE("ظ", "ﻅ", "ﻇ", "ﻆ", "ﻈ"),
	ADD_CHARLEFTRIGHTLIGATURE("ع", "ع", "ﻋ", "ﻌ", "ﻊ"),
	ADD_CHARLEFTRIGHTLIGATURE("غ", "ﻍ", "ﻏ", "ﻐ", "ﻎ"),
	ADD_CHARLEFTRIGHTLIGATURE("ف", "ﻑ", "ﻓ", "ﻔ", "ﻑ"),
	ADD_CHARLEFTRIGHTLIGATURE("ق", "ﻕ", "ﻗ", "ﻘ", "ﻖ"),
	ADD_CHARLEFTRIGHTLIGATURE("ك", "ﻙ", "ﻛ", "ﻜ", "ﻚ"),
	ADD_CHARLEFTRIGHTLIGATURE("گ", "ﮒ", "ﮔ", "ﮕ", "ﮓ"),
	ADD_CHARLEFTRIGHTLIGATURE("ل", "ﻝ", "ﻟ", "ﻠ", "ﻞ"),
	ADD_CHARLEFTRIGHTLIGATURE("م", "ﻡ", "ﻣ", "ﻤ", "ﻢ"),
	ADD_CHARLEFTRIGHTLIGATURE("ن", "ﻥ", "ﻧ", "ﻨ", "ﻦ"),
	ADD_CHARLEFTRIGHTLIGATURE("ه", "ﻩ", "ﻫ", "ﻬ", "ﻪ"),
	ADD_CHARLEFTLIGATURE("و", "ﻭ", "ﻮ"),
	ADD_CHARLEFTRIGHTLIGATURE("ي", "ﻱ", "ﻳ", "ﻴ", "ﻲ"),
	ADD_CHARLEFTLIGATURE("آ", "ﺁ", "ﺂ"),
	ADD_CHARLEFTLIGATURE("أ", "ﺃ", "ﺄ"),
	ADD_CHARLEFTLIGATURE("ة", "ﺓ", "ﺔ"),
	ADD_CHARLEFTLIGATURE("ى", "ﻯ", "ﻰ"),
	ADD_CHARLEFTLIGATURE("ؤ", "ﺅ", "ﺆ"),
	ADD_CHARLEFTLIGATURE("إ", "ﺇ", "ﺈ"),
	ADD_CHARLEFTRIGHTLIGATURE("ئ", "ﺉ", "ﺋ", "ﺌ", "ﺊ"),
};

#define ADD_COMB(c, cNext, c0, c1) {c, cNext, c0, c1}

static ArabicCombinaison g_aArabicCombinaisons[] =
{
	ADD_COMB("ل", "ا", "ﻻ", "ﻼ"),
	ADD_COMB("ل", "آ", "ﻵ", "ﻶ"),
	ADD_COMB("ل", "أ", "ﻷ", "ﻸ"),
	ADD_COMB("ل", "إ", "ﻹ", "ﻺ"),
};

ArabicCharacter* GetArabicCharacter(int currC)
{
	for(unsigned int i=0; i<sizeof(g_aArabicCharacters)/sizeof(ArabicCharacter); i++)
	{
		const char* pTmpIter = g_aArabicCharacters[i].c;
		if(currC == str_utf8_decode(&pTmpIter))
			return &g_aArabicCharacters[i];
	}
	return 0;
}

ArabicCombinaison* GetArabicCombinaison(int currC, int nextC)
{
	for(unsigned int i=0; i<sizeof(g_aArabicCombinaisons)/sizeof(ArabicCombinaison); i++)
	{
		const char* pTmpIter = g_aArabicCombinaisons[i].c;
		if(currC == str_utf8_decode(&pTmpIter))
		{
			pTmpIter = g_aArabicCombinaisons[i].cNext;
			if(nextC == str_utf8_decode(&pTmpIter))
				return &g_aArabicCombinaisons[i];
		}
	}
	return 0;
}

void ConvertArabicInput(const char* pInput, char* pTmp, char* pOutput)
{
	//Replace characters
	const char* pReadIter;
	char* pWriteIter;
	const char* pTmpIter;
	
	pReadIter = pInput;
	pWriteIter = pTmp;
	
	int prevC = 0;
	int currC = 0;
	int nextC = str_utf8_decode(&pReadIter);
	bool skip = false;
	do
	{
		if(!skip)
		{
			if(currC > 0)
			{
				int newC = currC;
				ArabicCharacter* currAC = GetArabicCharacter(currC);
				if(currAC)
				{
					ArabicCharacter* prevAC = GetArabicCharacter(prevC);
					bool left = (prevAC && prevAC->right && currAC->left);
					
					ArabicCombinaison* combinaison = GetArabicCombinaison(currC, nextC);
					if(combinaison)
					{
						if(left)
							pTmpIter = combinaison->c1;
						else
							pTmpIter = combinaison->c0;
						
						skip = true;
					}
					else
					{
						ArabicCharacter* nextAC = GetArabicCharacter(nextC);
						bool right = (nextAC && nextAC->left && currAC->right);
						
						if(left && right)
							pTmpIter = currAC->c11;
						else if(!left && right)
							pTmpIter = currAC->c01;
						else if(left && !right)
							pTmpIter = currAC->c10;
						else
							pTmpIter = currAC->c00;
					}
					
					newC = str_utf8_decode(&pTmpIter);
				}
				
				pWriteIter += str_utf8_encode(pWriteIter, newC);
			}
		}
		else
			skip = false;
		
		prevC = currC;
		currC = nextC;
		
		if(nextC == 0)
			break;
		
		nextC = str_utf8_decode(&pReadIter);
	}
	while(1);
	
	*pWriteIter = 0;
	
	//Reverse character order
	int Size = str_length(pTmp);
	int Iter = Size;
	pWriteIter = pOutput;
	while(Iter)
	{
		Iter = str_utf8_rewind(pTmp, Iter);
		
		pReadIter = pTmp+Iter;
		currC = str_utf8_decode(&pReadIter);
		if(currC >= 0)
			pWriteIter += str_utf8_encode(pWriteIter, currC);
	}
	*pWriteIter = 0;
}
