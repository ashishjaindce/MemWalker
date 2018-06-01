/*
 * gcc memwalker.c -o memwalker
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <termios.h>
#include <sys/mman.h>

#if 0
#define debug(fmt,arg...) \
        printf("%d %s %s: " fmt,__LINE__, __FILE__, __FUNCTION__, ##arg)
#else
#define debug(fmt,arg...) \
        do { } while (0)
#endif

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)

#define MWALK_LIST_MAX_NAME	100
#define MWALK_MAX_COMMENT_LEN	200
#define MWALK_MAX_LINE_LEN	500

struct khaleesi {
	char name[MWALK_LIST_MAX_NAME];
	struct list *list;
	struct list *listend;
};

struct list {
	struct khaleesi *head;
	struct list *prev, *next;
	char name[MWALK_LIST_MAX_NAME];
	void *data;
};

struct raven {
	unsigned long reg;
	int	bit;
	struct khaleesi *head;
};

struct memdata {
	unsigned int from, to, defval;
	char *comment;
};

struct memwalker_data {
	char name[100];
	struct khaleesi	*head, *headreg;
	int read;
};

static struct khaleesi *mwalk_create_list(char *name)
{
	struct khaleesi *head;

	head = calloc(1, sizeof(struct khaleesi));
	if (!head)
		return NULL;

	if (strlen(name) >= MWALK_LIST_MAX_NAME) {
		printf("%s: Namelen %d too long\n", __func__, strlen(name));
		return NULL;
	}
	strcpy(head->name, name);
	return head;
}

static struct list *mwalk_list_search(struct khaleesi *head, char *name)
{
	struct list *tmp = head->list;

	while (tmp != NULL) {
		if ((strncmp(tmp->name, name, strlen(name)) == 0) && (strlen(name) == strlen(tmp->name)))
			return tmp;
		tmp = tmp->next;
	}
	return NULL;
}

static int mwalk_list_add(struct khaleesi *head, char *name, void *data)
{
	struct list *tmp;
	struct list *tmp2;

	tmp = mwalk_list_search(head, name);
	if (tmp) {
		printf("%s name: %s in list\n", __func__, name, tmp->name);
		return 1;
	}
	tmp = calloc(1, sizeof(struct list));

	tmp->head = head;
	tmp->data = data;
	strncpy(tmp->name, name, strlen(name));
	if (head->list == NULL) {
		head->list = tmp;
		head->listend = tmp;
		return 0;
	}

	tmp2 = head->listend;
	head->listend = tmp;
	tmp2->next = tmp;
	tmp->prev = tmp2;
	return 0;
}

static unsigned int mwalk_get_value(unsigned long val, struct list *list)
{
	struct memdata	*night_watch;
	unsigned int start, ret = 0, i = 0;
	int count;

	night_watch = list->data;
	if (!night_watch) {
		printf("%s: no data\n", __func__);
		return 0;
	}
	count = night_watch->from - night_watch->to + 1;
	start = night_watch->to;
	while (count > 0) {
		ret |= ((val >> (start + i)) & 0x01) << i;
		count --;
		i++;
	}
	return ret;
}

static char *mwalk_get_comment(struct list *list)
{
	struct memdata	*night_watch;

	night_watch = list->data;
	if (!night_watch)
		return NULL;

	if (night_watch->comment)
		return night_watch->comment;

	return NULL;
}

static struct list *memwalker_search_list(struct khaleesi *head, char *name)
{
	struct list *list;

	list = mwalk_list_search(head, name);
	if (!list) {
		printf("name %s not found\n", name);
	}
	return list;
}

static int memwalker_read_mem(unsigned long reg, unsigned *val)
{
	int fd;
	int ret = -1;
	void *map_base, *virt_addr, *temp;

	if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
		close(fd);
		return ret;
	}
	map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ((reg & ~MAP_MASK)&0xFFFFFFFF));

	if(map_base == (void *) -1) {
		close(fd);
		return ret;
	}
	virt_addr = (int64_t *)((int8_t *)map_base + (reg & MAP_MASK));
	*val = *((unsigned *) virt_addr);
	munmap(0, MAP_SIZE);
	close(fd);
	return 2;
}

static int memwalker_is_comment(FILE *st, char *comment)
{
	int ret;
	
	ret = fread(comment, 1, 1, st);
	if (ret != 1) {
		comment[0] = 0;
		return -1;
	}

	if (comment[0] != '#') {
		comment[0] = 0;
		fseek(st, -1, SEEK_CUR);
		return 0;
	}
	fgets(comment, MWALK_MAX_COMMENT_LEN, st);
	printf("%s", comment);

	return 1;
}

static int memwalker_read_line(struct khaleesi *head, FILE *st, char *name, unsigned long *reg, char *comment)
{
	int ret = 0;
	char line[MWALK_MAX_LINE_LEN];
	char *tmp;

	ret = memwalker_is_comment(st, comment);
	if (ret < 0)
		return 0;
	while (ret == 1)
		ret = memwalker_is_comment(st, comment);

	tmp = fgets(line, MWALK_MAX_LINE_LEN, st);
	if (!tmp)
		return 0;

	ret = sscanf (line, "%s %lx %s\n", name, reg, comment);
	return ret;
}

static int memwalker_read_value(struct khaleesi *head, FILE *st, char *name, unsigned long *reg, int read)
{
	char	comment[MWALK_MAX_COMMENT_LEN];
	int	ret = 0;

	ret = memwalker_read_line(head, st, name, reg, comment);
	if ((ret < 1) || (ret > 3))
		return ret;

	if (ret == 1)
		read = 1;

	if (comment[0] != 0)
		printf("%s %lx %s\n", name, *reg, comment);

	if (read) {
		struct list *list;
		struct raven *crow;
		int tmp;

		list = memwalker_search_list(head, name);
		if (!list) {
			printf("%s: name %s not found\n", name);
			return -1;
		}
		crow = list->data;
		ret = memwalker_read_mem(crow->reg, reg);
	}
	return ret;
}

static int mwalk_read_between_line(FILE *st, char *name, unsigned long *from, unsigned long *to, unsigned long *def, char *comment)
{
	char line[MWALK_MAX_LINE_LEN];
	char *tmp;
	int ret = 0;

	ret = memwalker_is_comment(st, comment);
	if (ret < 0)
		return ret;
	while (ret == 1)
		ret = memwalker_is_comment(st, comment);
	if (ret < 0)
		return ret;

	tmp = fgets(line, MWALK_MAX_LINE_LEN, st);
	if (!tmp) {
		return -1;
	}

	sscanf(line, "%s", name);
	if (name[0] == '}') {
		return 0;
	}

	memset(comment, 0, MWALK_MAX_COMMENT_LEN);
	ret = sscanf (line, "%s %d %d %x %199c\n", name, from, to, def, comment);
	if (ret == 5) {
		int len = strlen(comment);
		comment[len] = 0;
		comment[len- 1] = 0;
	}
	return ret;
}

static int memwalker_descrition(struct memwalker_data *jon_snow, char *filename)
{
	FILE *winter;
	struct raven *crow;
	struct memdata	*night_watch;
	struct list *list;
	unsigned int house, from, to, def;
	char name[MWALK_LIST_MAX_NAME];
	char comment[MWALK_MAX_LINE_LEN];
	int ret;

	winter = fopen (filename, "r");
	if (!winter) {
		printf("%s: could not open %s\n", jon_snow->name, filename);
		exit (2);
	}

	fscanf (winter, "%s\n", name);
	jon_snow->head = mwalk_create_list(name);
	if (!jon_snow->head) {
		printf("%s: could not create list: %s\n", jon_snow->name, name);
		exit (3);
	}

	ret = fscanf (winter, "%s\n", name);
	if (strcmp(name, "{") != 0) {
		printf("missing open bracket\n");
		exit(5);
	}

	ret = fscanf (winter, "%s\n", name);
	while (ret == 1) {
		while (strcmp(name, "{") != 0) {
			int bit;

			fscanf (winter, "%x %d\n", &house, &bit);
			jon_snow->headreg = mwalk_create_list(name);
			if (!jon_snow->headreg) {
				printf("%s: could not create list: %s\n", jon_snow->name, name);
				exit (5);
			}
			crow = calloc(1, sizeof(struct raven));
			if (!crow) {
				printf("%s: could not create crow\n", jon_snow->name);
				exit (6);
			}
			crow->reg = house;
			crow->bit = bit;
			crow->head = jon_snow->headreg;
			ret = mwalk_list_add(jon_snow->head, name, (void *)crow);
			ret = fscanf (winter, "%s\n", name, bit);
			if (strcmp(name, "}") == 0)
				break;
		}
	
		/* parsing bit values */
		ret = mwalk_read_between_line(winter, name, &from, &to, &def, comment);
		while (ret > 0) {
			if (ret < 4) {
				printf("%s: missing data\n", jon_snow->name);
				exit (5);
			}
			night_watch = calloc(1, sizeof(struct memdata));
			if (!night_watch) {
				printf("%s: could not alloc mem for data\n", jon_snow->name);
				exit (5);
			}
			night_watch->from = from;
			night_watch->to = to;
			night_watch->defval = def;
			if (ret == 5) {
				int len = strlen(comment);

				night_watch->comment = calloc(1, len + 1);
				if (!night_watch->comment) {
					printf("%s: could not alloc mem for data\n", jon_snow->name);
					exit (6);
				}
				strncpy(night_watch->comment, comment, len);
			}
			ret = mwalk_list_add(jon_snow->headreg, name, (void *)night_watch);
			if (ret != 0) {
				printf("%s: list add error\n", jon_snow->name);
				exit (5);
			}
			ret = mwalk_read_between_line(winter, name, &from, &to, &def, comment);
		}
		ret = fscanf (winter, "%s\n", name);
		if (strcmp(name, "}") == 0)
			ret = 0;
	}
	fclose (winter);
	return 0;
}

int main(int argc, char *argv[])
{
	struct memwalker_data *jon_snow;
	struct list *list, *tmp;
	FILE *winter;
	int ret, i;
	char name[MWALK_LIST_MAX_NAME];
	unsigned int house, from, to, def, reg = 0;
	struct raven *crow;

	if ( argc < 2 ) {
		printf( "Usage: %s descriptionfile memfile \n", argv[0] );
		exit (1);
	}
	jon_snow = calloc(1, sizeof(struct memwalker_data));
	if (!jon_snow) {
		printf("%s: Could not get mem for jon_snow data\n", argv[0]);
		exit(1);
	}
	strcpy(jon_snow->name, argv[0]);

	memwalker_descrition(jon_snow, argv[1]);
	winter = fopen (argv[2], "r");
	if (!winter) {
		printf("%s: could not open %s\n", jon_snow->name, argv[2]);
		exit (2);
	}

	i = memwalker_read_value(jon_snow->head, winter, name, &reg, jon_snow->read);
	while (i > 0) {
		printf("\n\n--------------------------------------\n");
		list = memwalker_search_list(jon_snow->head, name);
		if (!list) {
			printf("%s: name %s not found\n", argv[0], name);
			exit (3);
		}
		crow = list->data;
		jon_snow->headreg = crow->head;
		printf("%s@0x%x %d bit val: 0x%x\n", jon_snow->headreg->name, crow->reg, crow->bit, reg);
		list = jon_snow->headreg->list;
		if (1) {
			while (list) {
				unsigned long val;
				char *comment;
				struct memdata  *night_watch = list->data;

				val = mwalk_get_value(reg, list);
				comment = mwalk_get_comment(list);
				if (comment)
					printf("%s (%d..%d) :  %x   %s\n", list->name, night_watch->to, night_watch->from, val, comment);
				else
					printf("%s %x\n", list->name, val);
				list = list->next;
			}
		}
		i = memwalker_read_value(jon_snow->head, winter, name, &reg, jon_snow->read);
	}

	fclose (winter);

	exit (0);
}
