/*****************************************************************************
 * twalk.c : implement twalk
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

/** search.h is not present so twalk has to be implemented */
#ifndef HAVE_SEARCH_H

#include <assert.h>
#include <stdlib.h>

typedef struct node {
    char         *key;
    struct node  *llink, *rlink;
} node_t;

/*	$NetBSD: twalk.c,v 1.2 1999/09/16 11:45:37 lukem Exp $	*/

/*
 * Tree search generalized from Knuth (6.2.2) Algorithm T just like
 * the AT&T man page says.
 *
 * The node_t structure is for internal use only, lint doesn't grok it.
 *
 * Written by reading the System V Interface Definition, not the code.
 *
 * Totally public domain.
 */

/* Walk the nodes of a tree */
static void
twalk_recurse(root, action, level)
	const node_t *root;	/* Root of the tree to be walked */
	void (*action) (const void *, VISIT, int);
	int level;
{
	assert(root != NULL);
	assert(action != NULL);

	if (root->llink == NULL && root->rlink == NULL)
		(*action)(root, leaf, level);
	else {
		(*action)(root, preorder, level);
		if (root->llink != NULL)
			twalk_recurse(root->llink, action, level + 1);
		(*action)(root, postorder, level);
		if (root->rlink != NULL)
			twalk_recurse(root->rlink, action, level + 1);
		(*action)(root, endorder, level);
	}
}

/* Walk the nodes of a tree */
void
twalk(vroot, action)
	const void *vroot;	/* Root of the tree to be walked */
	void (*action) (const void *, VISIT, int);
{
	if (vroot != NULL && action != NULL)
		twalk_recurse(vroot, action, 0);
}

#endif /* HAVE_SEARCH_H */
