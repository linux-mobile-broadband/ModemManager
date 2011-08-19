<!-- Generate a C header file from the Modem Manager specification.

Copyright (C) 2006, 2007 Collabora Limited

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
-->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">
  <!--Don't move the declaration of the HTML namespace up here - XMLNSs
  don't work ideally in the presence of two things that want to use the
  absence of a prefix, sadly. -->

  <xsl:strip-space elements="node interface property tp:errors tp:mapping tp:member"/>
  <xsl:template match="*" mode="identity">
    <xsl:copy>
      <xsl:apply-templates mode="identity"/>
    </xsl:copy>
  </xsl:template>
  <xsl:template name="CamelCase">
    <xsl:param name="text" />
    <xsl:variable name="lowercase" select="'abcdefghijklmnopqrstuvwxyz'"/>
    <xsl:variable name="uppercase" select="'ABCDEFGHIJKLMNOPQRSTUVWXYZ'"/>
    <xsl:if test="$text">
      <!-- get word to title-case -->
      <xsl:variable name="word">
	<xsl:choose>
	  <xsl:when test="contains($text, '_')">
	    <xsl:value-of select="substring-before($text, '_')"/>
	  </xsl:when>
	  <xsl:otherwise>
	    <xsl:value-of select="$text"/>
	  </xsl:otherwise>
	</xsl:choose>
      </xsl:variable>
      <!-- uppercase first character in word -->
      <xsl:value-of select="translate(substring($word,1,1), $lowercase, $uppercase)"/>
      <!-- lowercase all remaining characters in word -->
      <xsl:value-of select="translate(substring($word,2), $uppercase, $lowercase)"/>
      <!-- if any next chunk, recall template -->
      <xsl:call-template name="CamelCase">
	<xsl:with-param name="text" select="substring-after(substring($text,2), '_')"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>
  <xsl:template match="tp:docstring">
  </xsl:template>
  <xsl:template match="tp:realdocstring">
/* <xsl:apply-templates select="node()" mode="identity"/> */
  </xsl:template>
  <xsl:template match="tp:errors">
    <xsl:apply-templates/>
  </xsl:template>
  <xsl:template match="tp:generic-types">
    <xsl:call-template name="do-types"/>
  </xsl:template>
  <xsl:template name="do-types">
    <xsl:if test="tp:simple-type">
      <xsl:apply-templates select="tp:simple-type"/>
    </xsl:if>
    <xsl:if test="tp:enum">
      <xsl:apply-templates select="tp:enum"/>
    </xsl:if>
    <xsl:if test="tp:flags">
      <xsl:apply-templates select="tp:flags"/>
    </xsl:if>
  </xsl:template>
  <xsl:template match="tp:error">
    <xsl:apply-templates select="tp:docstring"/>
    <xsl:variable name="nameprefix">
        <xsl:value-of select="translate(substring-after(../@namespace, 'org.freedesktop.ModemManager.'),
			      'abcdefghijklmnopqrstuvwxyz. ',
			      'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
      </xsl:variable>
    <xsl:variable name="name">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
			    'ABCDEFGHIJKLMNOPQRSTUVWXYZ__')"/>
      </xsl:variable>
#define <xsl:value-of select="concat('MM_ERROR_', $nameprefix, '_', $name)"/> "<xsl:value-of select="translate(@name, ' ', '')"/>"</xsl:template>

  <xsl:template match="tp:flags">
/* <xsl:value-of select="@name"/> flag values */
    <xsl:apply-templates select="tp:docstring" />
    <xsl:variable name="value-prefix">
      <xsl:choose>
	<xsl:when test="@value-prefix">
	  <xsl:value-of select="@value-prefix"/>
	</xsl:when>
	<xsl:otherwise>
	  <xsl:value-of select="@name"/>
	</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="enum-name">
      <xsl:call-template name="CamelCase">
	<xsl:with-param name="text" select="substring-after(@name,'MM_')"/>
      </xsl:call-template>
    </xsl:variable>
typedef enum {
<xsl:for-each select="tp:flag">
<xsl:text>    </xsl:text><xsl:value-of select="concat($value-prefix, '_', @suffix)"/><xsl:text> = </xsl:text><xsl:value-of select="@value"/><xsl:text>,
</xsl:text>
</xsl:for-each>
<xsl:text>} </xsl:text><xsl:value-of select="concat('MM',$enum-name)"/><xsl:text>;
</xsl:text>
  </xsl:template>

  <xsl:template match="tp:enum">
/* <xsl:value-of select="@name"/> enum values */
    <xsl:apply-templates select="tp:docstring" />
    <xsl:variable name="value-prefix">
      <xsl:choose>
	<xsl:when test="@value-prefix">
	  <xsl:value-of select="@value-prefix"/>
	</xsl:when>
	<xsl:otherwise>
	  <xsl:value-of select="@name"/>
	</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:variable name="enum-name">
      <xsl:call-template name="CamelCase">
	<xsl:with-param name="text" select="substring-after(@name,'MM_')"/>
      </xsl:call-template>
    </xsl:variable>
typedef enum {
<xsl:for-each select="tp:enumvalue">
<xsl:text>    </xsl:text><xsl:value-of select="concat($value-prefix, '_', @suffix)"/><xsl:text> = </xsl:text><xsl:value-of select="@value"/><xsl:text>,
</xsl:text>
</xsl:for-each>
<xsl:text>} </xsl:text><xsl:value-of select="concat('MM',$enum-name)"/><xsl:text>;
</xsl:text>
  </xsl:template>

  <xsl:template match="tp:possible-errors/tp:error">
    <xsl:variable name="name" select="@name"/>
    <xsl:choose>
      <xsl:when test="tp:docstring">
        <xsl:apply-templates select="tp:docstring"/>
      </xsl:when>
      <xsl:when test="//tp:errors/tp:error[concat(../@namespace, '.', translate(@name, ' ', ''))=$name]/tp:docstring">
        <xsl:apply-templates select="//tp:errors/tp:error[concat(../@namespace, '.', translate(@name, ' ', ''))=$name]/tp:docstring"/> <em xmlns="http://www.w3.org/1999/xhtml">(generic description)</em>
      </xsl:when>
      <xsl:otherwise>
        (Undocumented.)
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="signal">
    <xsl:variable name="varname">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
			    'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
    </xsl:variable>
    <xsl:variable name="intname">
      <xsl:choose>
        <xsl:when test="starts-with(../@name, 'org.freedesktop.ModemManager.')">
          <xsl:value-of select="translate(substring-after(../@name, 'org.freedesktop.ModemManager.'),
			       'abcdefghijklmnopqrstuvwxyz. ',
			       'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
         </xsl:when>
         <xsl:otherwise>
           <xsl:text>MANAGER</xsl:text>
         </xsl:otherwise>
       </xsl:choose>
    </xsl:variable>
#define <xsl:value-of select="concat('MM_', $intname, '_SIGNAL_', $varname)"/> "<xsl:value-of select="@name"/>"</xsl:template>

  <xsl:template match="method">
    <xsl:variable name="varname">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
                            'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
    </xsl:variable>
    <xsl:variable name="intname">
      <xsl:choose>
        <xsl:when test="starts-with(../@name, 'org.freedesktop.ModemManager.')">
          <xsl:value-of select="translate(substring-after(../@name, 'org.freedesktop.ModemManager.'),
			       'abcdefghijklmnopqrstuvwxyz. ',
			       'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
         </xsl:when>
         <xsl:otherwise>
           <xsl:text>MANAGER</xsl:text>
         </xsl:otherwise>
       </xsl:choose>
    </xsl:variable>
#define <xsl:value-of select="concat('MM_', $intname, '_METHOD_', $varname)"/> "<xsl:value-of select="@name"/>"</xsl:template>

  <xsl:template match="tp:copyright">
  </xsl:template>

  <xsl:output method="text" indent="no" encoding="ascii"
    omit-xml-declaration="yes" />

  <xsl:template match="/tp:spec">
/* Generated Header file do not edit */

/*
 * <xsl:value-of select="tp:title"/>
 * version <xsl:value-of select="tp:version"/>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2011 Red Hat, Inc.
 */

#ifndef _MODEM_MANAGER_H_
#define _MODEM_MANAGER_H_

#define MM_DBUS_PATH    "/org/freedesktop/ModemManager"
#define MM_DBUS_SERVICE "org.freedesktop.ModemManager"

/**************
 * Interfaces *
 **************/
<xsl:for-each select="node/interface">
  <xsl:apply-templates select="tp:docstring"/>
  <xsl:variable name="varname">
    <xsl:choose>
      <xsl:when test="starts-with(@name, 'org.freedesktop.ModemManager.')">
	<xsl:value-of select="translate(substring-after(@name, 'org.freedesktop.ModemManager.'),
			      'abcdefghijklmnopqrstuvwxyz. ',
			      'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:text></xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:variable name="prefix">
    <xsl:choose>
      <xsl:when test="string-length($varname) > 0">
	<xsl:text>_</xsl:text>
      </xsl:when>
      <xsl:otherwise>
	<xsl:text></xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:if test="starts-with(@name, 'org.freedesktop.ModemManager')">
#define <xsl:value-of select="concat('MM_DBUS_INTERFACE', $prefix, $varname)"/> "<xsl:value-of select="@name"/>"</xsl:if></xsl:for-each>

/***********************
 * Methods/Enums/Flags *
 ***********************/
<xsl:for-each select="node/interface">
/*
 * Interface <xsl:value-of select="@name"/>
 */
  <xsl:apply-templates select="method"/>
  <xsl:if test="count(method[*])!=0">
  <xsl:text>
</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="signal"/>
  <xsl:if test="count(signal[*])!=0">
  <xsl:text>
</xsl:text>
  </xsl:if>
  <xsl:apply-templates select="tp:enum"/>
  <xsl:apply-templates select="tp:flags"/>
</xsl:for-each>

/**********
 * Errors *
 **********/
<xsl:apply-templates select="tp:errors"/>

#endif /*  _MODEM_MANAGER_H_ */

<!-- Ensure that the file ends with a newline -->
<xsl:text>
</xsl:text>
</xsl:template>
</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et: -->
