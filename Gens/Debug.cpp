#include "gens.h"
#ifdef GENS_DEBUG

#include <windows.h>
#include <stdio.h>

#include "G_main.h"
#include "G_Input.h"
#include "keycode.h"
#include "misc.h"
#include "gens.h"

#include "Debug.h"
#include "Cpu_68k.h"
#include "Star_68k.h"
#include "M68KD.h"
#include "Cpu_z80.h"
#include "z80.h"
#include "z80dis.h"
#include "Cpu_SH2.h"
#include "SH2.h"
#include "SH2D.h"
#include "mem_M68K.h"
#include "mem_S68K.h"
#include "mem_Z80.h"
#include "mem_SH2.h"
#include "vdp_io.h"
#include "vdp_rend.h"
#include "vdp_32X.h"
#include "LC89510.h"
#include "cd_aspi.h"
#include "resource.h"

extern "C" {BYTE* Dis68k(BYTE *buffer,int offset);}
extern "C" {int Current_PC;}
extern "C" {int Z80_Dasm (BYTE *buffer,BYTE *dest,unsigned PC);}

unsigned short Next_Word(void);
unsigned int Next_Long(void);

int Current_32X_FB = 0;
int adr_mem = 0, nb_inst = 1, pattern_adr = 0, pattern_pal;

static int current_offset=0;
static BOOL gotoaddress=FALSE;
static int thiskey;
char GString[1024];
char Dbg_Str[32];
char Dbg_EA_Str[16];
char Dbg_Size_Str[3];
char Dbg_Cond_Str[3];

HWND hDebugger=0;
BOOL CALLBACK Debugger(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
		int				i,j,k;
		static char		*text,*textp;
		static ULONG	*buffer,temp[100];
		static int		size=1;
		static int		which_cpu=0;
		static ULONG	mem_address=0;

		switch (message)
		{
		case WM_INITDIALOG :
			text=(char*)malloc(1024);
			buffer=(ULONG*)malloc(1024);
			if(buffer==0)
				EndDialog( hDlg, 0 );
			SetScrollRange(GetDlgItem(hDlg,IDC_SCROLLBAR1),SB_CTL,0,0x1000,FALSE);
			SetScrollRange(GetDlgItem(hDlg,IDC_SCROLLBAR2),SB_CTL,0,0x100,FALSE);
			return TRUE;
		case WM_COMMAND:
			switch (LOWORD(wParam))
			{
				case IDCANCEL:
					hDebugger=0;
					free(buffer);
					free(text);
					main68k_context.breakpoint=0;
					EndDialog( hDlg, 0 );
					break;
				case IDC_WHICHCPU:
					which_cpu++;
					if(which_cpu>2)
						which_cpu=0;
					switch(which_cpu)
					{
					case 0:
						SetDlgItemText(hDlg,IDC_WHICHCPU,"M68k");
						break;
					case 1:
						SetDlgItemText(hDlg,IDC_WHICHCPU,"Slave68k");
						break;
					case 2:
						SetDlgItemText(hDlg,IDC_WHICHCPU,"Z80");
						break;
					}
					SendMessage(hDlg,WM_USER,0,0); //update registers
					break;
				case IDC_STEP:
					if(which_cpu==0)
					{
					main68k_context.breakpoint=0;
					main68k_context.sr|=0x2700;
					main68k_tripOdometer();
					main68k_exec(1);
					main68k_context.breakpoint=main68k_context.pc;
					current_offset=main68k_context.pc;
					}
					else if(which_cpu==2)
					{
					M_Z80.breakpoint=0;
					z80_Clear_Odo(&M_Z80);
					z80_Exec(&M_Z80, 1);
					M_Z80.breakpoint=M_Z80.PC.d;
					current_offset=z80_Get_PC(&M_Z80);
					}
					SendMessage(hDlg,WM_USER,0,0); //update registers
					break;
				case IDC_BREAK:
					i=SendDlgItemMessage(hDebugger,IDC_DISASM,LB_GETCURSEL,0,0);
					if (i!=LB_ERR)
					{
					SendDlgItemMessage(hDebugger,IDC_DISASM,LB_GETTEXT,i,(LONG)text);
					i=strtoul(text,NULL,16);
						if(which_cpu==0)
						main68k_context.breakpoint=i;
						else if(which_cpu==2)
						M_Z80.breakpoint=M_Z80.BasePC+i;
					}
					else
					{
						if(which_cpu==0)
							main68k_context.breakpoint=main68k_context.pc;
						else if(which_cpu==2)
							M_Z80.breakpoint=M_Z80.PC.d;
					}
					break;
				case IDC_GO:
					main68k_context.breakpoint=0;
					M_Z80.breakpoint=0;
					break;
				case IDC_GOTO:
					GetDlgItemText(hDebugger,IDC_GOTOADDRESS,text,11);
					if(text[0]==0)
					{
						switch(which_cpu)
						{
						case 0:
							current_offset=main68k_context.pc;
							break;
						case 1:
							current_offset=sub68k_context.pc;
							break;
						case 2:
							current_offset=z80_Get_PC(&M_Z80);
							break;
						}
					}
					else
					current_offset=strtoul(text,NULL,16)&0x3FFFFF;
					SendMessage(hDlg,WM_USER,0xFE,0); //update only disasm
					break;
				case IDC_GOTOMEMORY:
					GetDlgItemText(hDebugger,IDC_MEMADDRESS,text,11);
					if(text[0]==0)
					mem_address=0;
					else
					mem_address=strtoul(text,NULL,16);
					SendMessage(hDlg,WM_USER,0,0); 
					break;
				case IDC_UPDATEMEM:
					GetDlgItemText(hDebugger,IDC_MEMORY,text,64*10);
					textp=text;
					for(i=0;i<8;i++)
					{
						temp[0]=strtoul(textp,NULL,16);
						textp=strchr(textp,':')+1;
						if((int)textp==1)
							break;
						for(j=0;j<8;j++)
						{
							temp[1]=(j&6)+1-(j&1);	//switch endian only for 68k
							k=strtoul(textp,NULL,16);
							if(temp[0]<0x600000)
								if(which_cpu==1)
								Ram_Prg[(temp[0]&0xFFFF)+temp[1]]=k;
								else
								Rom_Data[temp[0]+temp[1]]=k;
							else if(temp[0]<0xA08000)
								Ram_Z80[(temp[0]&0x7FFF)+j]=k;
							else
								Ram_68k[(temp[0]&0xFFFF)+temp[1]]=k;
							textp=strchr(textp,' ')+1;
						}
						textp=strchr(textp,'\n')+1;
					}
					SendMessage(hDlg,WM_USER,0,0); 
					break;

				case IDC_DISASM:
					switch(HIWORD(wParam))
					{
						case LBN_DBLCLK:
							i=SendDlgItemMessage(hDebugger,IDC_DISASM,LB_GETCURSEL,0,0);
							if (i!=LB_ERR)
							{
							SendDlgItemMessage(hDebugger,IDC_DISASM,LB_GETTEXT,i,(LONG)text);
							i=strtoul(text,NULL,16);
							if(which_cpu==0)
							main68k_context.breakpoint=i;
							else if(which_cpu==2)
							M_Z80.breakpoint=M_Z80.BasePC+i;
							}
							break;
					}
					break;
			}
			break;
		case WM_VSCROLL:
			if(lParam==(long)GetDlgItem(hDlg,IDC_SCROLLBAR1))
			{
				switch(LOWORD(wParam))
				{
					case SB_THUMBPOSITION:
	//					SetScrollPos(GetDlgItem(hDlg,IDC_SCROLLBAR1),SB_CTL,HIWORD(wParam),FALSE);
					case SB_THUMBTRACK:
						current_offset=HIWORD(wParam)*0x100;
						break;
					case SB_LINEDOWN:
						current_offset+=2;
						break;
					case SB_LINEUP:
						current_offset-=2;
						break;
					case SB_PAGEDOWN:
						current_offset+=40;
						break;
					case SB_PAGEUP:
						current_offset-=40;
						break;
				}
				current_offset&=0x1FFFFE;
				SetScrollPos(GetDlgItem(hDlg,IDC_SCROLLBAR1),SB_CTL,current_offset/0x100,FALSE);
				SendMessage(hDlg,WM_USER,0xFE,0); //update only disasm
			}
			else	//must be scrol bar 2
			{
				switch(LOWORD(wParam))
				{
					case SB_THUMBPOSITION:
						SetScrollPos(GetDlgItem(hDlg,IDC_SCROLLBAR2),SB_CTL,HIWORD(wParam),FALSE);
					case SB_THUMBTRACK:
						if((mem_address>>16)<=0x3F)
						mem_address=HIWORD(wParam)*0x100;
						else if((mem_address>>16)==0xA0)
						mem_address=0xA00000+(0x7FFF & HIWORD(wParam)*0x100);
						else if((mem_address>>16)>=0xE0)
						mem_address=0xE00000+(0xFFFF & HIWORD(wParam)*0x100);
						break;
					case SB_LINEDOWN:
						mem_address+=8;
						break;
					case SB_LINEUP:
						mem_address-=8;
						break;
					case SB_PAGEDOWN:
						mem_address+=40;
						break;
					case SB_PAGEUP:
						mem_address-=40;
						break;
				}
				SendMessage(hDlg,WM_USER,0,0); 
			}
			break; //WM_VSCROLL
		case WM_KEYDOWN:
			break;
		case WM_USER:		//update contents of debug windows
			if(wParam==0xFF){
				if(which_cpu==0)
				current_offset=main68k_context.pc;
				else
				current_offset=sub68k_context.pc;
			}
			//disasm 68k
			SendDlgItemMessage(hDlg,IDC_DISASM, LB_RESETCONTENT,0,0); //clear  all
			if(which_cpu<2)
			{
				Current_PC=current_offset; 
				for(i=0;i<28;i++)
				{
				j=Current_PC;
					if(which_cpu==0)
					k=(int)Dis68k(&Rom_Data[j],j);
					else
					k=(int)Dis68k(&Ram_Prg[j],j);
				SendDlgItemMessage(hDlg,IDC_DISASM, LB_INSERTSTRING,i,(LONG)k);
				}
			}
			else //Z80 disasm
			{
				Current_PC=current_offset&0x7FFF; 
				for(i=0;i<28;i++)
				{
				Current_PC+=Z80_Dasm(Ram_Z80+Current_PC,(BYTE *)GString,Current_PC);
				//z80dis((unsigned char *)Ram_Z80, (int *)&Current_PC, GString);
				SendDlgItemMessage(hDlg,IDC_DISASM, LB_INSERTSTRING,i,(LONG)GString);
				}

			}
			if(wParam==0xFE)	//only update disasm
				break;
			//print registers
			GString[0]=0;
			if(which_cpu<2)
			{
				for(i=0;i<8;i++)
				{
					if(which_cpu==0)
					sprintf(text,"D%i=%08X ",i,main68k_context.dreg[i]);
					else
					sprintf(text,"D%i=%08X ",i,sub68k_context.dreg[i]);
					strcat(GString,text);
					if((i&3)==3)
						strcat(GString,"\r\n");

				}	
				for(i=0;i<8;i++)
				{
					if(which_cpu==0)
					sprintf(text,"A%i=%08X ",i,main68k_context.areg[i]);
					else
					sprintf(text,"A%i=%08X ",i,sub68k_context.areg[i]);
					strcat(GString,text);
					if((i&3)==3)
						strcat(GString,"\r\n");

				}
				if(which_cpu==0)
				sprintf(text,"PC=%08X SV=%04X",main68k_context.pc,main68k_context.sr);
				else
				sprintf(text,"PC=%08X SV=%04X",sub68k_context.pc,sub68k_context.sr);
			}
			else //Z80 registers instead
			{
				sprintf(text,"AF=%04X ",M_Z80.AF.w.AF);
				strcat(GString,text);
				sprintf(text,"BC=%04X ",M_Z80.BC.w.BC);
				strcat(GString,text);
				sprintf(text,"DE=%04X ",M_Z80.DE.w.DE);
				strcat(GString,text);
				sprintf(text,"HL=%04X ",M_Z80.HL.w.HL);
				strcat(GString,text);
				sprintf(text,"IX=%04X ",M_Z80.IX.w.IX);
				strcat(GString,text);
				sprintf(text,"IY=%04X ",M_Z80.IY.w.IY);
				strcat(GString,text);
				sprintf(text,"\r\nPC=%04X\r\n",M_Z80.PC.w.PC-M_Z80.BasePC&0xFFFF);
				strcat(GString,text);
				sprintf(text,"SP=%04X\r\n",M_Z80.SP.w.SP);
				strcat(GString,text);
				sprintf(text,"FXWY=%04X\r\n",M_Z80.AF.w.FXYW);
			}
			strcat(GString,text);
			SetDlgItemText(hDlg,IDC_REGISTERS,GString);
			//print memory
			GString[0]=0;
			for(i=0;i<8;i++)
			{
				sprintf(text,"%04X:",mem_address+i*8);
				strcat(GString,text);
				for(j=0;j<8;j++)
				{
					k=(j&6)+1-(j&1); //switch endian only for 68k stuff

					if(mem_address>=0xE00000)
						sprintf(text,"%02X ",Ram_68k[(mem_address&0xFFFF)+k+(i*8)]);
					else if(mem_address>=0xA00000)
						sprintf(text,"%02X ",Ram_Z80[(mem_address&0x7FFF)+j+(i*8)]);
					else{
						if(which_cpu==0)
						sprintf(text,"%02X ",Rom_Data[(mem_address&0x7FFFFF)+k+(i*8)]);
						else
						sprintf(text,"%02X ",Ram_Prg[(mem_address&0x7FFFF)+k+(i*8)]);
					}
					strcat(GString,text);
				}
				strcat(GString,"\r\n");
			}
			SetDlgItemText(hDlg,IDC_MEMORY,GString);
			break;


		}
		return FALSE;
}


void Debug_Event(int key)
{
	int i;
	SH2_CONTEXT *sh;

	if (Debug & 1) sh = &M_SH2;
	else sh = &S_SH2;
	thiskey=key;
	switch(key)
	{
		case K_SPACE:
			current_offset=0;
			if ((Debug == 1) || (Debug == 3))
			{
				main68k_tripOdometer();
				main68k_exec(1);
			}
			else if (Debug == 2)
			{
				z80_Clear_Odo(&M_Z80);
				z80_Exec(&M_Z80, 1);
			}
			else if ((Debug >= 4) && (Debug < 7))
			{
				sub68k_tripOdometer();
				sub68k_exec(1);
			}
			else if ((Debug >= 7) && (Debug < 10))
			{
				SH2_Clear_Odo(sh);
				SH2_Exec(sh, 1);
			}
			break;
		case K_UP:
			adr_mem -= 0xC;
			break;

		case K_DOWN:
			adr_mem += 0xC;
			break;
		case K_PGDOWN:
			current_offset+=10;
			break;
		case K_PGUP:
			current_offset-=10;
			break;
		case K_G:
			gotoaddress=TRUE;
			break;
		case K_M:
			break;
		case K_ENTER:
			gotoaddress=FALSE;
			current_offset=current_offset-main68k_context.pc;
			break;
		case K_1:
		case K_2:
		case K_3:
		case K_4:
		case K_5:
		case K_6:
		case K_7:
		case K_8:
		case K_9:
			i|=key-1;
			break;
		case K_0:
			break;
		case K_A:
			i|=0xA;
			break;
		case K_B:
			i|=0xB;
			break;
		case K_C:
			i|=0xC;
			break;
		case K_D:
			i|=0xD;
			break;
		case K_E:
			i|=0xE;
			break;
		case K_F:
			i|=0xF;
			break;
	}
	if(gotoaddress)	
	{
	current_offset<<=4;
	current_offset|=i;
	}

}

			
unsigned short Next_Word(void)
{
	unsigned short val;
	
	if (Debug == 1) val = M68K_RW(Current_PC);
	else if (Debug >= 2) val = S68K_RW(Current_PC);
	else val = M68K_RW(Current_PC);

	Current_PC += 2;

	return(val);
}


unsigned int Next_Long(void)
{
	unsigned int val;
	
	if (Debug == 1)
	{
		val = M68K_RW(Current_PC);
		val <<= 16;
		val |= M68K_RW(Current_PC + 2);
	}
	else if (Debug >= 2)
	{
		val = S68K_RW(Current_PC);
		val <<= 16;
		val |= S68K_RW(Current_PC + 2);
	}
	else val = M68K_RW(Current_PC);

	Current_PC += 4;

	return(val);
}



void Refresh_M68k_Inst(void)
{
	unsigned int i, PC;

	Print_Text("** MAIN 68000 DEBUG **", 22, 24, 1, VERT);

	Current_PC = main68k_context.pc+current_offset;

	for(i = 1; i < 14; i++)
	{
		PC = Current_PC;
		sprintf(GString, "%.4X   %-33s\n", PC, M68KDisasm(Next_Word, Next_Long));
		if (PC == main68k_context.pc ) Print_Text(GString, 39, 1, (i << 3) + 5, ROUGE);
		else  Print_Text(GString, 39, 1, (i << 3) + 5, BLANC);
	}
}


void Refresh_S68k_Inst(void)
{
	unsigned int i, PC;

	Print_Text("** SUB 68000 DEBUG **", 22, 24, 1, VERT);

	Current_PC = sub68k_context.pc;
		
	for(i = 1; i < 14; i++)
	{
		PC = Current_PC;
		sprintf(GString, "%.4X   %-33s\n", PC, M68KDisasm(Next_Word, Next_Long));
		if (i == 1) Print_Text(GString, 39, 1, (i << 3) + 5, ROUGE);
		else  Print_Text(GString, 39, 1, (i << 3) + 5, BLANC);
	}
}


void Refresh_Z80_Inst(void)
{
	unsigned int i, PC;

	Print_Text("***** Z80 DEBUG *****", 22, 24, 1, VERT);

	PC = z80_Get_PC(&M_Z80);
		
	for(i = 1; i < 14; i++)
	{
//		z80dis((unsigned char *)Ram_Z80, (int *)&PC, GString);
		if (i == 1) Print_Text(GString, 39, 1, (i << 3) + 5, ROUGE);
		else Print_Text(GString, 39, 1, (i << 3) + 5, BLANC);
	}
}


void Refresh_SH2_Inst(int num)
{
	unsigned int i, PC;
	SH2_CONTEXT *sh;

	if (num)
	{
		Print_Text("** SLAVE SH2 DEBUG **", 22, 24, 1, VERT);
		sh = &S_SH2;
	}
	else
	{
		Print_Text("** MASTER SH2 DEBUG **", 22, 24, 1, VERT);
		sh = &M_SH2;
	}

	PC = (sh->PC - sh->Base_PC) - 4;
		
	for(i = 1; i < 14; i++, PC += 2)
	{
		SH2Disasm(GString, PC, SH2_Read_Word(sh, PC), 0);
		if (i == 1) Print_Text(GString, 39, 1, (i << 3) + 5, ROUGE);
		else Print_Text(GString, 39, 1, (i << 3) + 5, BLANC);
	}
}


void Refresh_M68k_Mem(void)
{
	unsigned int i, j, k, Adr; 

	Adr = adr_mem >> 1;

	Print_Text("** MAIN 68000 MEM **", 20, 24, 130, VERT);

	for(k = 0, j = Adr; k < 7; k++, j+= 6)
	{
		i = (j & 0x7FFF) << 1;
		sprintf(GString, "%.4X:%.4X %.4X %.4X %.4X %.4X %.4X\n", i, Ram_68k[i] + (Ram_68k[i + 1] << 8), Ram_68k[i + 2] + (Ram_68k[i + 3] << 8), Ram_68k[i + 4] + (Ram_68k[i + 5] << 8), Ram_68k[i + 6] + (Ram_68k[i + 7] << 8), Ram_68k[i + 8] + (Ram_68k[i + 9] << 8), Ram_68k[i + 10] + (Ram_68k[i + 11] << 8));
		Print_Text(GString, 34, 1, 146 + (k << 3), BLANC);
	}
}


void Refresh_S68k_Mem(void)
{
	unsigned int i, j, k, Adr; 

	Adr = adr_mem >> 1;

	Print_Text("** SUB 68000 MEM **", 19, 24, 130, VERT);

	for(k = 0, j = Adr; k < 7; k++, j+= 6)
	{
//		i = (j & 0x3FFFF) << 1;
//		sprintf(GString, "%.5X:%.4X %.4X %.4X %.4X %.4X %.4X\n", i, Ram_Prg[i] + (Ram_Prg[i + 1] << 8), Ram_Prg[i + 2] + (Ram_Prg[i + 3] << 8), Ram_Prg[i + 4] + (Ram_Prg[i + 5] << 8), Ram_Prg[i + 6] + (Ram_Prg[i + 7] << 8), Ram_Prg[i + 8] + (Ram_Prg[i + 9] << 8), Ram_Prg[i + 10] + (Ram_Prg[i + 11] << 8));
		i = (j & 0x1FFFF) << 1;
		sprintf(GString, "%.5X:%.4X %.4X %.4X %.4X %.4X %.4X\n", i, Ram_Word_1M[i] + (Ram_Word_1M[i + 1] << 8), Ram_Word_1M[i + 2] + (Ram_Word_1M[i + 3] << 8), Ram_Word_1M[i + 4] + (Ram_Word_1M[i + 5] << 8), Ram_Word_1M[i + 6] + (Ram_Word_1M[i + 7] << 8), Ram_Word_1M[i + 8] + (Ram_Word_1M[i + 9] << 8), Ram_Word_1M[i + 10] + (Ram_Word_1M[i + 11] << 8));
		Print_Text(GString, 35, 1, 146 + (k << 3), BLANC);
	}
}


void Refresh_Z80_Mem(void)
{
	unsigned int j, k; 

	Print_Text("***** Z80 MEM *****", 19, 24, 130, VERT);

	for(k = 0, j = adr_mem & 0xFFFF; k < 7; k++, j = (j + 12) & 0xFFFF)
	{
		sprintf(GString, "%.4X:%.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\n", j, Z80_ReadB(j + 0), Z80_ReadB(j + 1), Z80_ReadB(j + 2), Z80_ReadB(j + 3), Z80_ReadB(j + 4), Z80_ReadB(j + 5), Z80_ReadB(j + 6), Z80_ReadB(j + 7), Z80_ReadB(j + 8), Z80_ReadB(j + 9), Z80_ReadB(j + 10), Z80_ReadB(j + 11));
		Print_Text(GString, 35, 1, 146 + (k << 3), BLANC);
	}
}


void Refresh_SH2_Mem(void)
{
	unsigned int i, j, k, Adr; 

	Adr = adr_mem >> 1;

	Print_Text("*** SH2 CPU MEM ***", 19, 24, 130, VERT);

	for(k = 0, j = Adr; k < 7; k++, j+= 6)
	{
		i = (j & 0x1FFFF) << 1;
		sprintf(GString, "%.5X:%.4X %.4X %.4X %.4X %.4X %.4X\n", i, _32X_Ram[i] + (_32X_Ram[i + 1] << 8), _32X_Ram[i + 2] + (_32X_Ram[i + 3] << 8), _32X_Ram[i + 4] + (_32X_Ram[i + 5] << 8), _32X_Ram[i + 6] + (_32X_Ram[i + 7] << 8), _32X_Ram[i + 8] + (_32X_Ram[i + 9] << 8), _32X_Ram[i + 10] + (_32X_Ram[i + 11] << 8));
		Print_Text(GString, 35, 1, 146 + (k << 3), BLANC);
	}
}


void Refresh_M68k_State(void)
{
	Print_Text("** MAIN 68000 STATUS **", 23, 196, 130, VERT);
	
	sprintf(GString, "A0=%.8X A1=%.8X A2=%.8X X=%d\n", main68k_context.areg[0], main68k_context.areg[1], main68k_context.areg[2], (main68k_context.sr & 0x10)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 146, BLANC);
	sprintf(GString, "A3=%.8X A4=%.8X A5=%.8X N=%d\n", main68k_context.areg[3], main68k_context.areg[4], main68k_context.areg[5], (main68k_context.sr & 0x8)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 154, BLANC);
	sprintf(GString, "A6=%.8X A7=%.8X D0=%.8X Z=%d\n", main68k_context.areg[6], main68k_context.areg[7], main68k_context.dreg[0], (main68k_context.sr & 0x4)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 162, BLANC);
	sprintf(GString, "D1=%.8X D2=%.8X D3=%.8X V=%d\n", main68k_context.dreg[1], main68k_context.dreg[2], main68k_context.dreg[3], (main68k_context.sr & 0x2)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 170, BLANC);
	sprintf(GString, "D4=%.8X D5=%.8X D6=%.8X C=%d\n", main68k_context.dreg[4], main68k_context.dreg[5], main68k_context.dreg[6], (main68k_context.sr & 0x1)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 178, BLANC);
	sprintf(GString, "D7=%.8X PC=%.8X SR=%.4X\n", main68k_context.dreg[7], main68k_context.pc, main68k_context.sr);
	Print_Text(GString, String_Size(GString) - 1, 162, 186, BLANC);
	sprintf(GString, "Cycles=%.10d \n", main68k_context.odometer);
	Print_Text(GString, String_Size(GString) - 1, 162, 194, BLANC);
	sprintf(GString, "Bank for Z80 = %.8X\n", Bank_Z80);
	Print_Text(GString, String_Size(GString) - 1, 162, 202, BLANC);
//	sprintf(GString, "Bank = %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X\n", Rom_Bank[0], Rom_Bank[1], Rom_Bank[2], Rom_Bank[3], Rom_Bank[4], Rom_Bank[5], Rom_Bank[6], Rom_Bank[7]);
//	Print_Text(GString, String_Size(GString) - 1, 162, 210, BLANC);
}


void Refresh_S68k_State(void)
{
	Print_Text("** SUB 68000 STATUS **", 22, 196, 130, VERT);

	sprintf(GString, "A0=%.8X A1=%.8X A2=%.8X X=%d\n", sub68k_context.areg[0], sub68k_context.areg[1], sub68k_context.areg[2], (sub68k_context.sr & 0x10)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 146, BLANC);
	sprintf(GString, "A3=%.8X A4=%.8X A5=%.8X N=%d\n", sub68k_context.areg[3], sub68k_context.areg[4], sub68k_context.areg[5], (sub68k_context.sr & 0x8)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 154, BLANC);
	sprintf(GString, "A6=%.8X A7=%.8X D0=%.8X Z=%d\n", sub68k_context.areg[6], sub68k_context.areg[7], sub68k_context.dreg[0], (sub68k_context.sr & 0x4)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 162, BLANC);
	sprintf(GString, "D1=%.8X D2=%.8X D3=%.8X V=%d\n", sub68k_context.dreg[1], sub68k_context.dreg[2], sub68k_context.dreg[3], (sub68k_context.sr & 0x2)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 170, BLANC);
	sprintf(GString, "D4=%.8X D5=%.8X D6=%.8X C=%d\n", sub68k_context.dreg[4], sub68k_context.dreg[5], sub68k_context.dreg[6], (sub68k_context.sr & 0x1)?1:0);
	Print_Text(GString, String_Size(GString) - 1, 162, 178, BLANC);
	sprintf(GString, "D7=%.8X PC=%.8X SR=%.4X\n", sub68k_context.dreg[7], sub68k_context.pc, sub68k_context.sr);
	Print_Text(GString, String_Size(GString) - 1, 162, 186, BLANC);
	sprintf(GString, "Cycles=%.10d \n", sub68k_context.odometer);
	Print_Text(GString, String_Size(GString) - 1, 162, 194, BLANC);
//	sprintf(GString, "Bank for main 68K = %.8X\n", Bank_M68K);
//	Print_Text(GString, String_Size(GString) - 1, 162, 202, BLANC);
	sprintf(GString, "Bank for main 68K = %.8X\n", Bank_M68K);
	Print_Text(GString, String_Size(GString) - 1, 162, 202, BLANC);
}


void Refresh_Z80_State(void)
{
	Print_Text("***** Z80 STATUS *****", 22, 196, 130, VERT);

	sprintf(GString, "AF =%.4X BC =%.4X DE =%.4X HL =%.4X\n", z80_Get_AF(&M_Z80), M_Z80.BC.w.BC, M_Z80.DE.w.DE, M_Z80.HL.w.HL);
	Print_Text(GString, String_Size(GString) - 1, 162, 146, BLANC);
	sprintf(GString, "AF2=%.4X BC2=%.4X DE2=%.4X HL2=%.4X\n", z80_Get_AF2(&M_Z80), M_Z80.BC2.w.BC2, M_Z80.DE2.w.DE2, M_Z80.HL2.w.HL2);
	Print_Text(GString, String_Size(GString) - 1, 162, 154, BLANC);
	sprintf(GString, "IX =%.4X IY =%.4X SP =%.4X PC =%.4X\n", M_Z80.IX.w.IX, M_Z80.IY.w.IY, M_Z80.SP.w.SP, z80_Get_PC(&M_Z80));
	Print_Text(GString, String_Size(GString) - 1, 162, 162, BLANC);
	sprintf(GString, "IFF1=%d IFF2=%d I=%.2X R=%.2X IM=%.2X\n", M_Z80.IFF.b.IFF1, M_Z80.IFF.b.IFF2, M_Z80.I, M_Z80.R.b.R1, M_Z80.IM);
	Print_Text(GString, String_Size(GString) - 1, 162, 170, BLANC);
	sprintf(GString, "S=%d Z=%d Y=%d H=%d X=%d P=%d N=%d C=%d\n", (z80_Get_AF(&M_Z80) & 0x80) >> 7, (z80_Get_AF(&M_Z80) & 0x40) >> 6, (z80_Get_AF(&M_Z80) & 0x20) >> 5, (z80_Get_AF(&M_Z80) & 0x10) >> 4, (z80_Get_AF(&M_Z80) & 0x08) >> 3, (z80_Get_AF(&M_Z80) & 0x04) >> 2, (z80_Get_AF(&M_Z80) & 0x02) >> 1, (z80_Get_AF(&M_Z80) & 0x01) >> 0);
	Print_Text(GString, String_Size(GString) - 1, 162, 178, BLANC);
	sprintf(GString, "Status=%.2X ILine=%.2X IVect=%.2X\n", M_Z80.Status & 0xFF, M_Z80.IntLine, M_Z80.IntVect);
	Print_Text(GString, String_Size(GString) - 1, 162, 186, BLANC);
	sprintf(GString, "Bank68K=%.8X State=%.2X\n", M_Z80.Status & 0xFF, Bank_M68K, Z80_State);
	Print_Text(GString, String_Size(GString) - 1, 162, 194, BLANC);
}


void Refresh_SH2_State(int num)
{
	SH2_CONTEXT *sh;

	if (num)
	{
		Print_Text("** SLAVE SH2 STATUS **", 22, 196, 130, VERT);
		sh = &S_SH2;
	}
	else
	{
		Print_Text("** MASTER SH2 STATUS **", 22, 196, 130, VERT);
		sh = &M_SH2;
	}

	sprintf(GString, "R0=%.8X R1=%.8X R2=%.8X T=%d\n", SH2_Get_R(sh, 0), SH2_Get_R(sh, 1), SH2_Get_R(sh, 2), SH2_Get_SR(sh) & 1);
	Print_Text(GString, String_Size(GString) - 1, 162, 146, BLANC);
	sprintf(GString, "R3=%.8X R4=%.8X R5=%.8X S=%d\n", SH2_Get_R(sh, 3), SH2_Get_R(sh, 4), SH2_Get_R(sh, 5), (SH2_Get_SR(sh) >> 1) & 1);
	Print_Text(GString, String_Size(GString) - 1, 162, 154, BLANC);
	sprintf(GString, "R6=%.8X R7=%.8X R8=%.8X Q=%d\n", SH2_Get_R(sh, 6), SH2_Get_R(sh, 7), SH2_Get_R(sh, 8), (SH2_Get_SR(sh) >> 8) & 1);
	Print_Text(GString, String_Size(GString) - 1, 162, 162, BLANC);
	sprintf(GString, "R9=%.8X RA=%.8X RB=%.8X M=%d\n", SH2_Get_R(sh, 9), SH2_Get_R(sh, 0xA), SH2_Get_R(sh, 0xB), (SH2_Get_SR(sh) >> 9) & 1);
	Print_Text(GString, String_Size(GString) - 1, 162, 170, BLANC);
	sprintf(GString, "RC=%.8X RD=%.8X RE=%.8X I=%.1X\n", SH2_Get_R(sh, 0xC), SH2_Get_R(sh, 0xD), SH2_Get_R(sh, 0xE), (SH2_Get_SR(sh) >> 4) & 0xF);
	Print_Text(GString, String_Size(GString) - 1, 162, 178, BLANC);
	sprintf(GString, "RF=%.8X PC=%.8X SR=%.4X St=%.4X\n", SH2_Get_R(sh, 0xF), SH2_Get_PC(sh), SH2_Get_SR(sh), sh->Status & 0xFFFF);
	Print_Text(GString, String_Size(GString) - 1, 162, 186, BLANC);
	sprintf(GString, "GBR=%.8X VBR=%.8X PR=%.8X\n", SH2_Get_GBR(sh), SH2_Get_VBR(sh), SH2_Get_PR(sh));
	Print_Text(GString, String_Size(GString) - 1, 162, 194, BLANC);
	sprintf(GString, "MACH=%.8X MACL=%.8X IL=%.2X IV=%.2X\n", SH2_Get_MACH(sh), SH2_Get_MACL(sh), sh->INT.Prio, sh->INT.Vect);
	Print_Text(GString, String_Size(GString) - 1, 162, 202, BLANC);
}


void Refresh_VDP_State(void)
{
	int tmp;

	Print_Text("**** VDP STATUS ****", 20, 200, 1, VERT);

	sprintf(GString, "Setting register: 1=%.2X 2=%.2X 3=%.2X 4=%.2X", VDP_Reg.Set1, VDP_Reg.Set2, VDP_Reg.Set3, VDP_Reg.Set4);
	Print_Text(GString, String_Size(GString), 162, 14, BLANC);
	sprintf(GString, "Pattern Adr: ScrA=%.2X ScrB=%.2X Win=%.2X", VDP_Reg.Pat_ScrA_Adr, VDP_Reg.Pat_ScrB_Adr, VDP_Reg.Pat_Win_Adr);
	Print_Text(GString, String_Size(GString), 162, 22, BLANC);
	sprintf(GString, "Sprite Attribut Adr: Low=%.2X High=%.2X", VDP_Reg.Spr_Att_Adr, VDP_Reg.Reg6);
	Print_Text(GString, String_Size(GString), 162, 30, BLANC);
	sprintf(GString, "H Scroll Adr: Low=%.2X High=%.2X",VDP_Reg.H_Scr_Adr, VDP_Reg.Reg14);
	Print_Text(GString, String_Size(GString), 162, 38, BLANC);
	sprintf(GString, "H Interrupt=%.2X    Auto Inc=%.2X", VDP_Reg.H_Int, VDP_Reg.Auto_Inc);
	Print_Text(GString, String_Size(GString), 162, 46, BLANC);
	sprintf(GString, "BG Color: Low=%.2X Med=%.2X High=%.2X", VDP_Reg.BG_Color, VDP_Reg.Reg8, VDP_Reg.Reg9);
	Print_Text(GString, String_Size(GString), 162, 54, BLANC);
	sprintf(GString, "Scroll Size=%.2X    Window Pos: H=%.2X V=%.2X", VDP_Reg.Scr_Size, VDP_Reg.Win_H_Pos, VDP_Reg.Win_V_Pos);
	Print_Text(GString, String_Size(GString), 162, 62, BLANC);
	sprintf(GString, "DMA Lenght: Low=%.2X High=%.2X", VDP_Reg.DMA_Lenght_L, VDP_Reg.DMA_Lenght_H);
	Print_Text(GString, String_Size(GString), 162, 70, BLANC);
	sprintf(GString, "DMA Source Adr: Low=%.2X Med=%.2X High=%.2X", VDP_Reg.DMA_Src_Adr_L, VDP_Reg.DMA_Src_Adr_M, VDP_Reg.DMA_Src_Adr_H);
	Print_Text(GString, String_Size(GString), 162, 78, BLANC);
	tmp = Read_VDP_Status();
	sprintf(GString, "V Int Happened %d  Sprite overflow %d", (tmp >> 7) & 1, (tmp >> 6) & 1);
	Print_Text(GString, String_Size(GString), 162, 86, BLANC);
	sprintf(GString, "Collision Spr  %d  Odd Frame in IM %d", (tmp >> 5) & 1, (tmp >> 4) & 1);
	Print_Text(GString, String_Size(GString), 162, 94, BLANC);
	sprintf(GString, "During V Blank %d  During H Blank  %d", (tmp >> 3) & 1, (tmp >> 2) & 1);
	Print_Text(GString, String_Size(GString), 162, 102, BLANC);
	sprintf(GString, "DMA Busy %d  PAL Mode %d Line Num %d", (tmp >> 1) & 1, tmp & 1, VDP_Current_Line);
	Print_Text(GString, String_Size(GString), 162, 110, BLANC);
	sprintf(GString, "VDP Int =%.2X DMA_Lenght=%.4X", VDP_Int, DMAT_Lenght);
	Print_Text(GString, String_Size(GString), 162, 118, BLANC);
}


void Refresh_VDP_Pattern(void)
{
	unsigned int i;
	
	Print_Text("******** VDP PATTERN ********", 29, 28, 0, VERT);

	for(i = 0; i < 20; i++)
	{
		sprintf(GString, "%.4X", (pattern_adr & 0xFFFF) + 0x200 * i);
		Print_Text(GString, String_Size(GString), 2, (i << 3) + 11, BLANC);
	}
	
	Cell_8x8_Dump(&VRam[pattern_adr & 0xFFFF], pattern_pal);
}


void Refresh_VDP_Palette(void)
{
	unsigned int i, j, k;

	Print_Text("******** VDP PALETTE ********", 29, 180, 0, ROUGE);

	for(i = 0; i < 16; i++)
	{
		for(j = 0; j < 8; j++)
		{
			for(k = 0; k < 8; k++)
			{
				MD_Screen[(336  * 10) + (k * 336) + 180 + (i * 8) + j] = MD_Palette[i + 0];
				MD_Screen[(336  * 18) + (k * 336) + 180 + (i * 8) + j] = MD_Palette[i + 16];
				MD_Screen[(336  * 26) + (k * 336) + 180 + (i * 8) + j] = MD_Palette[i + 32];
				MD_Screen[(336  * 34) + (k * 336) + 180 + (i * 8) + j] = MD_Palette[i + 48];
			}
		}
	}

	Print_Text("******** VDP CONTROL ********", 29, 180, 60, BLANC);

	sprintf(GString, "Status : %.4X", Read_VDP_Status());
	Print_Text(GString, strlen(GString), 176, 70, BLANC);
	sprintf(GString, "Flag : %.2X       Data : %.8X", Ctrl.Flag, Ctrl.Data);
	Print_Text(GString, strlen(GString), 176, 78, BLANC);
	sprintf(GString, "Write : %.2X      Access : %.2X", Ctrl.Write, Ctrl.Access);
	Print_Text(GString, strlen(GString), 176, 86, BLANC);
	sprintf(GString, "Address : %.4X  DMA_Mode : %.2X", Ctrl.Address, Ctrl.DMA_Mode);
	Print_Text(GString, strlen(GString), 176, 94, BLANC);
	sprintf(GString, "DMA adr: %.8X  DMA len: %.4X", VDP_Reg.DMA_Address, VDP_Reg.DMA_Lenght);
	Print_Text(GString, strlen(GString), 176, 102, BLANC);
	sprintf(GString, "DMA : %.2X", Ctrl.DMA);
	Print_Text(GString, strlen(GString), 176, 110, BLANC);

	sprintf(GString, "Sprite Liste :");
	Print_Text(GString, strlen(GString), 176, 126, BLANC);

	for(i = 0; i < 10; i++)
	{
		sprintf(GString, "%d %d %d %d %d", Sprite_Struct[i].Pos_X , Sprite_Struct[i].Pos_Y, Sprite_Struct[i].Size_X, Sprite_Struct[i].Size_Y, Sprite_Struct[i].Num_Tile);
		Print_Text(GString, strlen(GString), 176, 134 + (i * 8), BLANC);
	}
}


void Refresh_SegaCD_State(void)
{
	Print_Text("** SEGACD STATUS **", 20, 200, 1, VERT);

	sprintf(GString, "GE00=%.4X GE02=%.4X CD00=%.4X CD02=%.4X", M68K_RW(0xA12000), M68K_RW(0xA12002), S68K_RW(0xFF8000), S68K_RW(0xFF8002));
	Print_Text(GString, String_Size(GString), 162, 14, BLANC);
	sprintf(GString, "GE04=%.4X GE06=%.4X CD04=%.4X CD06=%.4X", M68K_RW(0xA12004), M68K_RW(0xA12006), S68K_RW(0xFF8004), 0x0000);
	Print_Text(GString, String_Size(GString), 162, 22, BLANC);
	sprintf(GString, "GE0A=%.4X GE0C=%.4X CD0A=%.4X CD0C=%.4X", M68K_RW(0xA1200A), M68K_RW(0xA1200C), S68K_RW(0xFF800A), S68K_RW(0xFF800C));
	Print_Text(GString, String_Size(GString), 162, 30, BLANC);
	sprintf(GString, "GD0E=%.4X", S68K_RW(0xFF800E));
	Print_Text(GString, String_Size(GString), 162, 38, BLANC);
	sprintf(GString, "GD10=%.4X GD12=%.4X GD14=%.4X GD16=%.4X", S68K_RW(0xFF8010), S68K_RW(0xFF8012), S68K_RW(0xFF8014), S68K_RW(0xFF8016));
	Print_Text(GString, String_Size(GString), 162, 46, BLANC);
	sprintf(GString, "GD18=%.4X GD1A=%.4X GD1C=%.4X GD1E=%.4X", S68K_RW(0xFF8018), S68K_RW(0xFF801A), S68K_RW(0xFF801C), S68K_RW(0xFF801E));
	Print_Text(GString, String_Size(GString), 162, 54, BLANC);
	sprintf(GString, "GD20=%.4X GD22=%.4X GD24=%.4X GD26=%.4X", S68K_RW(0xFF8020), S68K_RW(0xFF8022), S68K_RW(0xFF8024), S68K_RW(0xFF8026));
	Print_Text(GString, String_Size(GString), 162, 62, BLANC);
	sprintf(GString, "GD28=%.4X GD2A=%.4X GD2C=%.4X GD2E=%.4X", S68K_RW(0xFF8028), S68K_RW(0xFF802A), S68K_RW(0xFF802C), S68K_RW(0xFF802E));
	Print_Text(GString, String_Size(GString), 162, 70, BLANC);
	sprintf(GString, "CD30=%.4X CD32=%.4X CD34=%.4X CD36=%.4X", S68K_RW(0xFF8030), S68K_RW(0xFF8032), S68K_RW(0xFF8034), S68K_RW(0xFF8036));
	Print_Text(GString, String_Size(GString), 162, 78, BLANC);
	sprintf(GString, "CD38=%.4X CD3A=%.4X CD3E=%.4X CD40=%.4X", S68K_RW(0xFF8038), S68K_RW(0xFF803A), S68K_RW(0xFF803E), S68K_RW(0xFF8040));
	Print_Text(GString, String_Size(GString), 162, 86, BLANC);
	sprintf(GString, "CD42=%.4X CD44=%.4X CD48=%.4X CD4A=%.4X", S68K_RW(0xFF8042), S68K_RW(0xFF8044), S68K_RW(0xFF8048), S68K_RW(0xFF804A));
	Print_Text(GString, String_Size(GString), 162, 94, BLANC);
	sprintf(GString, "CD4C=%.4X CD4E=%.4X CD50=%.4X CD52=%.4X", S68K_RW(0xFF804C), S68K_RW(0xFF804E), S68K_RW(0xFF8050), S68K_RW(0xFF8052));
	Print_Text(GString, String_Size(GString), 162, 102, BLANC);
	sprintf(GString, "CD58=%.4X CD5A=%.4X CD5C=%.4X CD5E=%.4X", S68K_RW(0xFF8058), S68K_RW(0xFF805A), S68K_RW(0xFF805C), S68K_RW(0xFF805E));
	Print_Text(GString, String_Size(GString), 162, 110, BLANC);
	sprintf(GString, "CD60=%.4X CD62=%.4X CD64=%.4X CD66=%.4X", S68K_RW(0xFF8060), S68K_RW(0xFF8062), S68K_RW(0xFF8064), S68K_RW(0xFF8066));
	Print_Text(GString, String_Size(GString), 162, 118, BLANC);
}


void Refresh_32X_State(void)
{
	Print_Text("*** 32X STATUS ***", 20, 200, 1, VERT);

	sprintf(GString, "M000=%.4X S000=%.4X M004=%.4X M006=%.4X", SH2_Read_Word(&M_SH2, 0x4000), SH2_Read_Word(&S_SH2, 0x4000), SH2_Read_Word(&M_SH2, 0x4004), SH2_Read_Word(&M_SH2, 0x4006));
	Print_Text(GString, String_Size(GString), 162, 14, BLANC);
	sprintf(GString, "M008=%.4X M00A=%.4X M00C=%.4X M00E=%.4X", SH2_Read_Word(&M_SH2, 0x4008), SH2_Read_Word(&M_SH2, 0x400A), SH2_Read_Word(&M_SH2, 0x400C), SH2_Read_Word(&M_SH2, 0x400E));
	Print_Text(GString, String_Size(GString), 162, 22, BLANC);
	sprintf(GString, "M010=%.4X M012=%.4X M014=%.4X M016=%.4X", SH2_Read_Word(&M_SH2, 0x4010), SH2_Read_Word(&M_SH2, 0x4012), SH2_Read_Word(&M_SH2, 0x4014), SH2_Read_Word(&M_SH2, 0x4016));
	Print_Text(GString, String_Size(GString), 162, 30, BLANC);
	sprintf(GString, "M020=%.4X M022=%.4X M024=%.4X M026=%.4X", SH2_Read_Word(&M_SH2, 0x4020), SH2_Read_Word(&M_SH2, 0x4022), SH2_Read_Word(&M_SH2, 0x4024), SH2_Read_Word(&M_SH2, 0x4026));
	Print_Text(GString, String_Size(GString), 162, 38, BLANC);
	sprintf(GString, "M028=%.4X M02A=%.4X M02C=%.4X M02E=%.4X", SH2_Read_Word(&M_SH2, 0x4028), SH2_Read_Word(&M_SH2, 0x402A), SH2_Read_Word(&M_SH2, 0x402C), SH2_Read_Word(&M_SH2, 0x402E));
	Print_Text(GString, String_Size(GString), 162, 46, BLANC);
	sprintf(GString, "M030=%.4X M032=%.4X M034=%.4X M036=%.4X", SH2_Read_Word(&M_SH2, 0x4030), SH2_Read_Word(&M_SH2, 0x4032), SH2_Read_Word(&M_SH2, 0x4034), SH2_Read_Word(&M_SH2, 0x4036));
	Print_Text(GString, String_Size(GString), 162, 54, BLANC);
	sprintf(GString, "M100=%.4X M102=%.4X M104=%.4X M106=%.4X", SH2_Read_Word(&M_SH2, 0x4100), SH2_Read_Word(&M_SH2, 0x4102), SH2_Read_Word(&M_SH2, 0x4104), SH2_Read_Word(&M_SH2, 0x4106));
	Print_Text(GString, String_Size(GString), 162, 62, BLANC);
	sprintf(GString, "M108=%.4X M10A=%.4X M10C=%.4X M10E=%.4X", SH2_Read_Word(&M_SH2, 0x4108), SH2_Read_Word(&M_SH2, 0x410A), SH2_Read_Word(&M_SH2, 0x410C), SH2_Read_Word(&M_SH2, 0x410E));
	Print_Text(GString, String_Size(GString), 162, 70, BLANC);
}


void Refresh_CDC_State(void)
{
	Print_Text("** CDC STATUS **", 16, 200, 1, VERT);

	sprintf(GString, "COMIN=%.2X IFSTAT=%.2X DBC=%.4X", CDC.COMIN, CDC.IFSTAT, CDC.DBC);
	Print_Text(GString, String_Size(GString), 162, 14, BLANC);
	sprintf(GString, "HEAD=%.8X PT=%.4X WA=%.4X", CDC.HEAD, CDC.PT, CDC.WA);
	Print_Text(GString, String_Size(GString), 162, 22, BLANC);
	sprintf(GString, "STAT=%.8X CTRL=%.8X", CDC.STAT, CDC.CTRL);
	Print_Text(GString, String_Size(GString), 162, 30, BLANC);
	sprintf(GString, "DAC=%.4X IFCTRL=%.2X", CDC.DAC, CDC.IFCTRL);
	Print_Text(GString, String_Size(GString), 162, 38, BLANC);
}


void Refresh_Word_Ram_Pattern(void)
{
	unsigned int i;
	
	Print_Text("****** WORD RAM PATTERN ******", 29, 28, 0, VERT);

	for(i = 0; i < 20; i++)
	{
		sprintf(GString, "%.4X", (pattern_adr & 0x3FFFF) + 0x200 * i);
		Print_Text(GString, String_Size(GString), 2, (i << 3) + 11, BLANC);
	}
	
	Cell_16x16_Dump(&Ram_Word_2M[pattern_adr & 0x3FFFF], pattern_pal);
}


void Update_Debug_Screen(void)
{
	char text[100];
	memset(MD_Screen, 0, 336 * 240 * 2);
	
	if (Debug & 0x100) Do_VDP_Only();
	else switch(Debug)
	{
		default:
		case 1:		// Main 68000
			Refresh_M68k_Mem();
			Refresh_M68k_Inst();
			Refresh_M68k_State();
			Refresh_VDP_State();
			if(gotoaddress)
			{
			sprintf(text,"offset:%08X  key:%02X",current_offset,thiskey);
			Print_Text(text, 150, 0, 120, ROUGE);
			}
			break;

		case 2:		// Z80
			Refresh_Z80_Mem();
			Refresh_Z80_Inst();
			Refresh_Z80_State();
			break;

		case 3:		// Genesis VDP
			Refresh_VDP_Pattern();
			Refresh_VDP_Palette();
			break;

		case 4:		// Sub 68000 Reg
			Refresh_S68k_Mem();
			Refresh_S68k_Inst();
			Refresh_S68k_State();
			Refresh_SegaCD_State();
			break;

		case 5:		// Sub 68000 CDC
			Refresh_S68k_Mem();
			Refresh_S68k_Inst();
			Refresh_S68k_State();
			Refresh_CDC_State();
			break;

		case 6:		// Vector chip pattern
			Refresh_Word_Ram_Pattern();
			break;

		case 7:		// Main SH2
			Refresh_SH2_Mem();
			Refresh_SH2_Inst(0);
			Refresh_SH2_State(0);
			Refresh_32X_State();
			break;

		case 8:		// Sub SH2
			Refresh_SH2_Mem();
			Refresh_SH2_Inst(1);
			Refresh_SH2_State(1);
			Refresh_32X_State();
			break;

		case 9:		// 32X VDP
			_32X_VDP_Draw(Current_32X_FB);
			break;
	}

	Sleep(10);
}




#endif //GENS_DEBUG