<xsl:stylesheet version = '1.0'
     xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>
  <xsl:output omit-xml-declaration="yes"/>

  <xsl:template match="//interface[@name][1]">
    <xsl:value-of select="@name"/>
  </xsl:template>

  <!-- this strips out all the non-tag text so that
       we don't emit lots of unwanted inter-tag whitespace  -->
  <xsl:template match="text()"/>

</xsl:stylesheet> 
